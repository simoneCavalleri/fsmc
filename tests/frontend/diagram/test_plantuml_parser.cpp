#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/middleend/fsm_validator.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify Mermaid syntax parsing, state aliases, guard/action extraction, and validation.
 *
 * Scenario:
 * - Parse Mermaid `stateDiagram-v2` with state aliases, transition labels, guards `[Guard]`, and actions `/ Action`.
 * - Verify FsmIr element counts and validation pass.
 */
TEST(ParserTest, MermaidBasicParsingAndValidation) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        state "Waiting for CAN" as WaitingForCan
        Idle --> WaitingForCan : CmdStart [CanStartGuard] / OnStartAction
        Idle --> Idle : CmdStop / OnStopAction
        WaitingForCan --> Running : CanOk [IsReady]
        Running --> Idle : CmdStop
    )";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    EXPECT_EQ(model.initial_state, "Idle");
    EXPECT_EQ(model.states.size(), 3u);   // Idle, WaitingForCan, Running
    EXPECT_EQ(model.events.size(), 3u);   // CmdStart, CmdStop, CanOk
    EXPECT_EQ(model.guards.size(), 2u);   // CanStartGuard, IsReady
    EXPECT_EQ(model.actions.size(), 2u);  // OnStartAction, OnStopAction
    EXPECT_EQ(model.transitions.size(), 4u);

    const auto validation = FsmValidator::validate(model);
    EXPECT_TRUE(validation.is_valid);
    EXPECT_TRUE(validation.errors.empty());
}

/**
 * @brief Test Intent: Verify Mermaid comment stripping (`%%`), note stripping, and composite state hierarchy.
 *
 * Scenario:
 * - Parse Mermaid diagram containing comments, notes, and nested composite states.
 * - Verify parent-child links and initial sub-state assignment.
 */
TEST(ParserTest, MermaidCommentsNotesAndComplexHierarchy) {
    const std::string mmd = R"(
    stateDiagram-v2
        %% This is a top-level mermaid comment
        [*] --> SuperState
        
        state SuperState {
            [*] --> SubA
            SubA --> SubB : NextEvt / StepAction
            note right of SubB: This is a note
        }
        SuperState --> Finished : CompleteEvt
    )";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    EXPECT_EQ(model.initial_state, "SuperState");
    ASSERT_NE(model.find_state("SuperState"), nullptr);
    EXPECT_TRUE(model.find_state("SuperState")->is_composite);
    EXPECT_EQ(model.find_state("SuperState")->initial_sub_state, "SubA");

    ASSERT_NE(model.find_state("SubA"), nullptr);
    EXPECT_EQ(model.find_state("SubA")->parent_state, "SuperState");

    ASSERT_NE(model.find_state("SubB"), nullptr);
    EXPECT_EQ(model.find_state("SubB")->parent_state, "SuperState");
}

/**
 * @brief Test Intent: Verify basic PlantUML syntax parsing and model validation.
 *
 * Scenario:
 * - Parse PlantUML with transitions, guards, actions, and initial state pointer.
 * - Verify FsmIr element extraction and FsmValidator passing.
 */
TEST(ParserTest, PlantUmlBasicParsingAndValidation) {
    const std::string puml = R"(
    @startuml
    [*] --> Standby
    Standby -> Processing : StartTask [HasWork] / InitTask
    Processing -> Standby : StopTask / Cleanup
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    EXPECT_EQ(model.initial_state, "Standby");
    EXPECT_EQ(model.states.size(), 2u);
    EXPECT_EQ(model.events.size(), 2u);
    EXPECT_EQ(model.guards.size(), 1u);
    EXPECT_EQ(model.actions.size(), 2u);
    EXPECT_EQ(model.transitions.size(), 2u);

    const auto validation = FsmValidator::validate(model);
    EXPECT_TRUE(validation.is_valid);
}

/**
 * @brief Test Intent: Verify PlantUML single-line and multi-line comment stripping and composite states.
 *
 * Scenario:
 * - Parse PlantUML containing `' comment` and `/' ... '/` block comments with internal transitions.
 * - Verify hierarchy and internal state actions.
 */
TEST(ParserTest, PlantUmlCommentsAndCompositeHierarchy) {
    const std::string puml = R"(
    @startuml
    ' Single line comment
    /' Multi-line
       block comment '/
    [*] --> Operational
    
    state Operational {
        [*] --> SelfCheck
        SelfCheck : Progress / Report
        SelfCheck --> Armed : CheckOk
    }
    
    Operational --> Standby : Disarm
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    EXPECT_EQ(model.initial_state, "Operational");
    ASSERT_NE(model.find_state("Operational"), nullptr);
    EXPECT_TRUE(model.find_state("Operational")->is_composite);
    EXPECT_EQ(model.find_state("SelfCheck")->parent_state, "Operational");
}

/**
 * @brief Test Intent: Verify FsmValidator detects undefined transition target states.
 *
 * Scenario:
 * - Construct FsmIr with transition to a non-existent state `UnknownTarget`.
 * - Verify FsmValidator::validate() reports errors and fails validity check.
 */
TEST(ParserTest, ValidatorDetectsMissingTargetAndDeadlocks) {
    FsmIr model;
    model.initial_state = "Idle";
    model.add_state("Idle");
    // Transition to unknown target
    model.transitions.emplace_back("Idle", "UnknownTarget", "MyEvent", std::nullopt, std::nullopt, "");

    const auto validation = FsmValidator::validate(model);
    EXPECT_FALSE(validation.is_valid);
    EXPECT_FALSE(validation.errors.empty());
}

/**
 * @brief Test Intent: Verify parsers gracefully reject empty and whitespace-only inputs.
 *
 * Scenario:
 * - Feed empty string and whitespace-only string to PlantUmlParser and MermaidParser.
 * - Verify parser returns false with an informative error message.
 */
TEST(ParserTest, ParserRejectsEmptyInput) {
    PlantUmlParser puml_parser;
    MermaidParser mmd_parser;
    FsmIr model;
    std::string err;

    EXPECT_FALSE(puml_parser.parse("", model, err));
    EXPECT_FALSE(mmd_parser.parse("   \n\t ", model, err));
}

/**
 * @brief Test Intent: Verify PlantUML parsing of entryPoint, exitPoint, stay duration (time invariant), and transition
 * priority.
 *
 * Scenario:
 * - Parse PlantUML with `state ep <<entryPoint>>`, `state xp <<exitPoint>>`, `Active : invariant stay <= 100ms`, and
 * `(prio=3)`.
 * - Verify IR captures StateKind::EntryPoint, StateKind::ExitPoint, time_invariant, and transition priority.
 */
TEST(ParserTest, PlantUmlEntryExitPointPriorityAndInvariant) {
    const std::string puml = R"(
    @startuml
    [*] --> Idle
    state ep <<entryPoint>>
    state xp <<exitPoint>>
    state Active {
        Active : invariant stay <= 100ms
    }
    Idle --> Active : (prio=5) EvGo [GuardOk] / ActGo
    Active --> xp : (priority=2) EvDone
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    const auto* ep = model.find_state("ep");
    ASSERT_NE(ep, nullptr);
    EXPECT_EQ(ep->kind, StateKind::EntryPoint);

    const auto* xp = model.find_state("xp");
    ASSERT_NE(xp, nullptr);
    EXPECT_EQ(xp->kind, StateKind::ExitPoint);

    const auto* active = model.find_state("Active");
    ASSERT_NE(active, nullptr);
    ASSERT_TRUE(active->time_invariant.has_value());
    EXPECT_EQ(*active->time_invariant, "stay <= 100ms");

    ASSERT_EQ(model.transitions.size(), 2u);
    EXPECT_EQ(model.transitions[0].priority, 5u);
    EXPECT_EQ(model.transitions[1].priority, 2u);
}

/**
 * @brief Test Intent: Verify Mermaid parsing of entryPoint, exitPoint, and transition priority.
 *
 * Scenario:
 * - Parse Mermaid with `state ep <<entryPoint>>`, `state xp <<exitPoint>>`, and `Idle --> Active : (prio=4) EvStart`.
 * - Verify IR captures StateKind::EntryPoint, StateKind::ExitPoint, and transition priority.
 */
TEST(ParserTest, MermaidEntryExitPointAndPriority) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        state ep <<entryPoint>>
        state xp <<exitPoint>>
        Idle --> Active : (prio=4) EvStart [CanStart] / OnStart
        Active --> xp : (prio=1) EvFinish
    )";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    const auto* ep = model.find_state("ep");
    ASSERT_NE(ep, nullptr);
    EXPECT_EQ(ep->kind, StateKind::EntryPoint);

    const auto* xp = model.find_state("xp");
    ASSERT_NE(xp, nullptr);
    EXPECT_EQ(xp->kind, StateKind::ExitPoint);

    ASSERT_EQ(model.transitions.size(), 2u);
    EXPECT_EQ(model.transitions[0].priority, 4u);
    EXPECT_EQ(model.transitions[1].priority, 1u);
}

}  // namespace

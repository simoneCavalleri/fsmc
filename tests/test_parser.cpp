#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "fsm/frontend/mermaid_parser.hpp"
#include "fsm/frontend/plantuml_parser.hpp"
#include "fsm/middleend/fsm_validator.hpp"

using namespace fsm::codegen;

namespace {

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

TEST(ParserTest, ParserRejectsEmptyInput) {
    PlantUmlParser puml_parser;
    MermaidParser mmd_parser;
    FsmIr model;
    std::string err;

    EXPECT_FALSE(puml_parser.parse("", model, err));
    EXPECT_FALSE(mmd_parser.parse("   \n\t ", model, err));
}

}  // namespace

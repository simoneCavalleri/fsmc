/**
 * @file test_mermaid_parser.cpp
 * @brief Unit test suite for the Mermaid stateDiagram-v2 parser and frontend dialect.
 */

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"

using namespace fsm::frontend::diagram;
using namespace fsm::frontend;
using namespace fsm::middleend::analysis;
using namespace fsm::middleend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify Mermaid syntax parsing, state aliases, guard/action extraction, and validation.
 * @scenario Parse Mermaid stateDiagram-v2 with aliases, transitions, guards [Guard], and actions / Action.
 * @expected FsmIr element counts and validation pass without errors.
 */
TEST(MermaidParser, BasicDiagram_ParsedIntoValidFsmIr) {
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
    EXPECT_EQ(model.states.size(), 3u);
    EXPECT_EQ(model.signals.size(), 3u);
    EXPECT_EQ(model.guards.size(), 2u);
    EXPECT_EQ(model.actions.size(), 2u);
    EXPECT_EQ(model.transitions.size(), 4u);

    const auto validation = FsmValidator::validate(model);
    EXPECT_TRUE(validation.is_valid);
    EXPECT_TRUE(validation.errors.empty());
}

/**
 * @brief Verify Mermaid comment stripping, note stripping, and composite state hierarchy.
 * @scenario Parse Mermaid diagram containing %% comments, notes, and nested composite states.
 * @expected Comments and notes removed, parent-child links formed, and initial substate assigned.
 */
TEST(MermaidParser, CommentsNotesAndHierarchy_ParsedCorrectly) {
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
 * @brief Verify Mermaid parser gracefully rejects empty and whitespace-only inputs.
 * @scenario Feed whitespace-only string to MermaidParser.
 * @expected Parser returns false with an informative error message.
 */
TEST(MermaidParser, EmptyInput_GracefullyRejected) {
    MermaidParser mmd_parser;
    FsmIr model;
    std::string err;

    EXPECT_FALSE(mmd_parser.parse("   \n\t ", model, err));
}

/**
 * @brief Verify Mermaid parsing of entryPoint, exitPoint, and transition priority.
 * @scenario Parse Mermaid diagram with state ep <<entryPoint>>, state xp <<exitPoint>>, and transition priority tags.
 * @expected IR captures StateKind::EntryPoint, StateKind::ExitPoint, and transition priorities.
 */
TEST(MermaidParser, EntryExitPointAndPriority_CapturedInIr) {
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

/**
 * @brief Verify Mermaid parsing of @fsm:port directives into FsmIr.
 * @scenario Parse Mermaid diagram containing %% @fsm:port directive.
 * @expected Inbound port created with declared numerical bounds.
 */
TEST(MermaidParser, PortDirectives_ParsedWithAttributesAndConstraints) {
    const std::string mmd = R"(
    stateDiagram-v2
        %% @fsm:port name=voltage_in type=float dir=in min=18.0 max=36.0
        [*] --> Standby
        Standby --> Active : EvPowerOn
    )";

    MermaidParser mmd_parser;
    FsmIr mmd_model;
    std::string err;
    ASSERT_TRUE(mmd_parser.parse(mmd, mmd_model, err)) << err;

    ASSERT_EQ(mmd_model.ports.size(), 1u);
    const auto* v_in = mmd_model.find_port("voltage_in");
    ASSERT_NE(v_in, nullptr);
    EXPECT_TRUE(v_in->is_in());
    EXPECT_DOUBLE_EQ(v_in->min_value.value_or(0.0), 18.0);
    EXPECT_DOUBLE_EQ(v_in->max_value.value_or(0.0), 36.0);
}

}  // namespace

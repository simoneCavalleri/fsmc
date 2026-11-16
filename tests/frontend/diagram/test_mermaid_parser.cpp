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

/**
 * @brief Verify Mermaid parsing of nested square brackets in transition guards and atomic_guard_details preservation.
 * @scenario Parse Mermaid transition with array subscript in guard [buf[0] > 10 && ready].
 * @expected Full bracketed guard is extracted without truncation and atomic GuardModel entries preserve raw_expression.
 */
TEST(MermaidParser, NestedSquareBracketsInGuard_PreservesRawExpressionDetails) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> StateA
        StateA --> StateB : EvGo [buf[0] > 10 && ready] / OnGo
    )";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    ASSERT_EQ(model.transitions.size(), 1u);
    EXPECT_EQ(model.transitions[0].event, "EvGo");
    ASSERT_TRUE(model.transitions[0].guard.has_value());
    EXPECT_NE(model.transitions[0].guard->find("fsm::and_<"), std::string::npos);

    ASSERT_EQ(model.guards.size(), 2u);
    bool found_array_expr = false;
    bool found_ready_expr = false;
    for (const auto& g : model.guards) {
        if (g.raw_expression.has_value() && *g.raw_expression == "buf[0] > 10") {
            found_array_expr = true;
        }
        if (g.raw_expression.has_value() && *g.raw_expression == "ready") {
            found_ready_expr = true;
        }
    }
    EXPECT_TRUE(found_array_expr);
    EXPECT_TRUE(found_ready_expr);
}

/**
 * @brief Verify MermaidParser recognizes '--' orthogonal region separator inside composite states.
 * @scenario Mermaid stateDiagram-v2 with two regions inside Active separated by '--'.
 * @expected Active has StateKind::Parallel and 2 orthogonal_regions with correct initial states.
 */
TEST(MermaidParserTest, OrthogonalSeparatorCreatesParallelRegions) {
    const std::string mmd = R"mmd(
stateDiagram-v2
    [*] --> Active
    state Active {
        state Reg1 {
            [*] --> S1
            S1 --> S2 : EvA
        }
        --
        state Reg2 {
            [*] --> T1
            T1 --> T2 : EvB
        }
    }
)mmd";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << err;

    const StateNode* active = model.find_state("Active");
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->kind, StateKind::Parallel);
    ASSERT_EQ(active->orthogonal_regions.size(), 2u);
    EXPECT_EQ(active->orthogonal_regions[0].id, "Reg1");
    EXPECT_EQ(active->orthogonal_regions[0].initial_state_id, "S1");
    EXPECT_EQ(active->orthogonal_regions[1].id, "Reg2");
    EXPECT_EQ(active->orthogonal_regions[1].initial_state_id, "T1");
}

/**
 * @brief Verify Mermaid states inside orthogonal regions keep pinned_parent when referenced by root Fork/Join
 * transitions.
 * @scenario Mermaid stateDiagram-v2 with Par { R1 { A1 -> A2 } -- R2 { B1 -> B2 } } and root A2 -> J / B2 -> J.
 * @expected A2 stays inside R1 and B2 stays inside R2 after normalize_hierarchy().
 */
TEST(MermaidParserTest, ForkJoinWithExternalTransitionsPreservesRegionHierarchy) {
    const std::string mmd = R"mmd(
stateDiagram-v2
    [*] --> Init
    state F <<fork>>
    state J <<join>>
    state Par {
        state R1 {
            [*] --> A1
            A1 --> A2 : EvA
        }
        --
        state R2 {
            [*] --> B1
            B1 --> B2 : EvB
        }
    }
    Init --> F : EvStart
    F --> A1
    F --> B1
    A2 --> J : EvDone
    B2 --> J : EvDone
    J --> Finished
)mmd";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << err;

    const StateNode* a2 = model.find_state("A2");
    const StateNode* b2 = model.find_state("B2");
    ASSERT_NE(a2, nullptr);
    ASSERT_NE(b2, nullptr);
    EXPECT_EQ(a2->parent_state, "R1");
    EXPECT_EQ(b2->parent_state, "R2");
}

/**
 * @brief Verify synthesized assignment action names do not collide across entry, exit, and transitions.
 * @scenario Parse Mermaid where Ready entry, Ready exit, and two transitions all assign to variable x.
 * @expected All four ActionSignature entries have distinct names and retain their own assignment expressions.
 */
TEST(MermaidParser, DistinctAssignmentActionNames_AcrossLifecycleAndTransitions) {
    const std::string mmd = R"mmd(
stateDiagram-v2
    [*] --> Ready
    Ready : entry / x = 10
    Ready : exit / x += 1
    Ready --> Running : START [x >= 10] / x = x * 2
    Ready --> Running : FAST / x = 99
)mmd";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << err;

    const StateNode* ready = model.find_state("Ready");
    ASSERT_NE(ready, nullptr);
    ASSERT_EQ(ready->entry_actions.size(), 1u);
    ASSERT_EQ(ready->exit_actions.size(), 1u);
    ASSERT_EQ(model.transitions.size(), 2u);
    ASSERT_TRUE(model.transitions[0].transition_action.has_value());
    ASSERT_TRUE(model.transitions[1].transition_action.has_value());

    const std::string n_entry = ready->entry_actions[0].name;
    const std::string n_exit = ready->exit_actions[0].name;
    const std::string n_t0 = model.transitions[0].transition_action->name;
    const std::string n_t1 = model.transitions[1].transition_action->name;

    EXPECT_NE(n_entry, n_exit);
    EXPECT_NE(n_entry, n_t0);
    EXPECT_NE(n_exit, n_t0);
    EXPECT_NE(n_t0, n_t1);

    EXPECT_EQ(ready->entry_actions[0].assignments[0].expression, "10");
    EXPECT_EQ(ready->exit_actions[0].assignments[0].expression, "1");
    EXPECT_EQ(model.transitions[0].transition_action->assignments[0].expression, "x * 2");
    EXPECT_EQ(model.transitions[1].transition_action->assignments[0].expression, "99");
}

}  // namespace

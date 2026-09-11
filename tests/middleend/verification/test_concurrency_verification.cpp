/**
 * @file test_concurrency_verification.cpp
 * @brief Unit tests for concurrency safety, data race detection, and determinism verification passes.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/passes/determinism_enforcement_pass.hpp"
#include "fsm/middleend/passes/orthogonal_interference_pass.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend::passes;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify OrthogonalInterferencePass detects concurrent data races in parallel (AND) states.
 * @scenario Parallel state with orthogonal regions RegA and RegB concurrently mutating variable 'battery_level'.
 * @expected Pass emits a SafetyCritical diagnostic 'W_CONCURRENT_DATA_RACE'.
 */
TEST(OrthogonalInterference, ConcurrentConflictingVariableWrites_EmitsDataRaceError) {
    FsmIr ir;
    ir.name = "ConcurrentFSM";
    ir.initial_state = "ParallelState";

    auto& p_state = ir.add_or_get_state("ParallelState", "", StateKind::Parallel);

    OrthogonalRegion reg_a;
    reg_a.name = "RegA";
    reg_a.state_ids = {"StateA1", "StateA2"};
    p_state.orthogonal_regions.push_back(reg_a);

    OrthogonalRegion reg_b;
    reg_b.name = "RegB";
    reg_b.state_ids = {"StateB1", "StateB2"};
    p_state.orthogonal_regions.push_back(reg_b);

    // Transition in RegA mutating battery_level
    TransitionEdge t_a;
    t_a.source = "StateA1";
    t_a.target = "StateA2";
    t_a.event = "TickA";
    ActionSignature act_a("ActA", "ActA");
    act_a.assignments.emplace_back("battery_level", "battery_level - 1");
    t_a.transition_action = act_a;
    ir.add_transition(t_a);

    // Transition in RegB ALSO mutating battery_level -> Data Race!
    TransitionEdge t_b;
    t_b.source = "StateB1";
    t_b.target = "StateB2";
    t_b.event = "TickB";
    ActionSignature act_b("ActB", "ActB");
    act_b.assignments.emplace_back("battery_level", "battery_level + 10");
    t_b.transition_action = act_b;
    ir.add_transition(t_b);

    OrthogonalInterferencePass pass;
    DiagnosticEngine diag;
    pass.run(ir, diag);

    EXPECT_TRUE(diag.has_errors());
    bool found_race = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "W_CONCURRENT_DATA_RACE" && d.severity == DiagnosticSeverity::Fatal) {
            found_race = true;
            break;
        }
    }
    EXPECT_TRUE(found_race);
}

/**
 * @brief Verify DeterminismEnforcementPass canonical priority sorting and collision detection.
 * @scenario Multiple transitions from Idle on event StartCmd with non-sequential priorities (10 and 1).
 * @expected Transitions are deterministically sorted by ascending priority (1 before 10).
 */
TEST(DeterminismEnforcement, MultipleTransitionsFromSameSource_SortedByAscendingPriority) {
    FsmIr ir;
    ir.name = "PriorityFSM";
    ir.initial_state = "Idle";
    ir.add_state("Idle");
    ir.add_state("FastMode");
    ir.add_state("SlowMode");

    TransitionEdge t_slow;
    t_slow.source = "Idle";
    t_slow.target = "SlowMode";
    t_slow.event = "StartCmd";
    t_slow.priority = 10;
    ir.add_transition(t_slow);

    TransitionEdge t_fast;
    t_fast.source = "Idle";
    t_fast.target = "FastMode";
    t_fast.event = "StartCmd";
    t_fast.priority = 1;  // Higher priority
    ir.add_transition(t_fast);

    DeterminismEnforcementPass pass;
    DiagnosticEngine diag;
    pass.run(ir, diag);

    // Verify FastMode (priority 1) is sorted before SlowMode (priority 10)
    ASSERT_EQ(ir.transitions.size(), 2u);
    EXPECT_EQ(ir.transitions[0].target, "FastMode");
    EXPECT_EQ(ir.transitions[0].priority, 1u);
    EXPECT_EQ(ir.transitions[1].target, "SlowMode");
    EXPECT_EQ(ir.transitions[1].priority, 10u);
}

/**
 * @brief Verify determinism enforcement detects non-deterministic collisions on identical-priority branches.
 * @scenario Two transitions from 'Running' on 'CmdEmergency' both configured with identical priority 0.
 * @expected Pass detects ambiguity and emits fatal diagnostic error.
 */
TEST(DeterminismEnforcement, ConflictingTransitionsSamePriority_EmitsDeterminismError) {
    FsmIr model;
    model.name = "CollisionFSM";

    TransitionEdge t1;
    t1.id = "t1";
    t1.source = "Running";
    t1.target = "Fault";
    t1.event = "CmdEmergency";
    t1.priority = 0;
    model.add_transition(t1);

    TransitionEdge t2;
    t2.id = "t2";
    t2.source = "Running";
    t2.target = "Idle";
    t2.event = "CmdEmergency";
    t2.priority = 0;
    model.add_transition(t2);

    DiagnosticEngine diag;
    DeterminismEnforcementPass pass;
    pass.run(model, diag);
    EXPECT_TRUE(diag.has_errors());
}

}  // namespace

/**
 * @file test_constant_folding_and_minimization.cpp
 * @brief Unit tests for ConstantFoldingPass guard evaluation and StateMinimizationPass equivalence partitioning.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/passes/constant_folding_pass.hpp"
#include "fsm/middleend/passes/state_minimization_pass.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend::passes;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify ConstantFoldingPass folds tautological guards and eliminates false transitions.
 * @scenario Transitions with guards '1 == 1' (tautology), '0 == 1' (contradiction), and '5 > 2' (tautology).
 * @expected Contradictory transition pruned; tautological transitions stripped of guards to unconditional.
 */
TEST(ConstantFolding, TautologicalAndContradictoryGuards_EvaluatedAndPruned) {
    FsmIr model;
    model.name = "ConstantGuardModel";
    model.add_state("S1");
    model.add_state("S2");

    TransitionEdge t1;
    t1.source = "S1";
    t1.target = "S2";
    t1.event = "Ev1";
    t1.guard = "1 == 1";
    model.add_transition(t1);

    TransitionEdge t2;
    t2.source = "S1";
    t2.target = "S2";
    t2.event = "Ev2";
    t2.guard = "0 == 1";
    model.add_transition(t2);

    TransitionEdge t3;
    t3.source = "S1";
    t3.target = "S2";
    t3.event = "Ev3";
    t3.guard = "5 > 2";
    model.add_transition(t3);

    ConstantFoldingPass pass;
    DiagnosticEngine diag;
    bool ok = pass.run(model, diag);

    EXPECT_TRUE(ok);
    // T2 should be pruned, remaining T1 and T3 should have nullopt guard
    EXPECT_EQ(model.transitions.size(), 2u);

    for (const auto& t : model.transitions) {
        EXPECT_NE(t.event, "Ev2");
        EXPECT_FALSE(t.guard.has_value());
    }
}

/**
 * @brief Verify StateMinimizationPass merges behaviorally equivalent states.
 * @scenario States Equiv1 and Equiv2 transition to Target on identical event 'Step' with identical guards/actions.
 * @expected Equiv1 and Equiv2 merged into a single representative state, minimizing state count.
 */
TEST(StateMinimization, BehaviorallyEquivalentStates_MergedIntoCanonicalRepresentative) {
    FsmIr model;
    model.name = "MinimizableModel";
    model.add_state("Init");
    model.initial_state = "Init";
    model.add_state("Target");
    model.add_state("Equiv1");
    model.add_state("Equiv2");

    TransitionEdge t_init1;
    t_init1.source = "Init";
    t_init1.target = "Equiv1";
    t_init1.event = "Go1";
    model.add_transition(t_init1);

    TransitionEdge t_init2;
    t_init2.source = "Init";
    t_init2.target = "Equiv2";
    t_init2.event = "Go2";
    model.add_transition(t_init2);

    TransitionEdge t_eq1;
    t_eq1.source = "Equiv1";
    t_eq1.target = "Target";
    t_eq1.event = "Step";
    model.add_transition(t_eq1);

    TransitionEdge t_eq2;
    t_eq2.source = "Equiv2";
    t_eq2.target = "Target";
    t_eq2.event = "Step";
    model.add_transition(t_eq2);

    StateMinimizationPass pass;
    DiagnosticEngine diag;
    bool ok = pass.run(model, diag);

    EXPECT_TRUE(ok);
    // Equiv1 and Equiv2 should be collapsed into one canonical representative
    EXPECT_EQ(model.states.size(), 3u);
}

/**
 * @brief Verify states with different numbers of guarded transitions for the same event are not merged.
 */
TEST(StateMinimization, MultipleGuardedTransitionsForSameEvent_NotMergedWhenDifferent) {
    FsmIr model;
    model.name = "GuardedTransitionsModel";
    model.add_state("Init");
    model.initial_state = "Init";
    auto& target1 = model.add_state("Target1");
    target1.entry_actions.push_back(ActionSignature{"on_target1"});
    auto& target2 = model.add_state("Target2");
    target2.entry_actions.push_back(ActionSignature{"on_target2"});
    model.add_state("BranchingState");
    model.add_state("SingleState");

    // BranchingState has two guarded transitions on Ev
    TransitionEdge t1;
    t1.source = "BranchingState";
    t1.target = "Target1";
    t1.event = "Ev";
    t1.guard = "x > 0";
    model.add_transition(t1);

    TransitionEdge t2;
    t2.source = "BranchingState";
    t2.target = "Target2";
    t2.event = "Ev";
    t2.guard = "x <= 0";
    model.add_transition(t2);

    // SingleState only has one guarded transition on Ev
    TransitionEdge t3;
    t3.source = "SingleState";
    t3.target = "Target1";
    t3.event = "Ev";
    t3.guard = "x > 0";
    model.add_transition(t3);

    StateMinimizationPass pass;
    DiagnosticEngine diag;
    bool ok = pass.run(model, diag);

    // BranchingState and SingleState must NOT be merged
    EXPECT_FALSE(ok);
    EXPECT_EQ(model.states.size(), 5u);
    EXPECT_NE(model.find_state("BranchingState"), nullptr);
    EXPECT_NE(model.find_state("SingleState"), nullptr);
}

/**
 * @brief Verify states with identical guarded transitions in different insertion orders are correctly merged.
 */
TEST(StateMinimization, MultipleGuardedTransitionsForSameEvent_MergedWhenIdenticalRegardlessOfOrder) {
    FsmIr model;
    model.name = "OrderIndependentModel";
    model.add_state("Init");
    model.initial_state = "Init";
    auto& target1 = model.add_state("Target1");
    target1.entry_actions.push_back(ActionSignature{"on_target1"});
    auto& target2 = model.add_state("Target2");
    target2.entry_actions.push_back(ActionSignature{"on_target2"});
    model.add_state("StateA");
    model.add_state("StateB");

    // StateA transitions: Target1 then Target2
    TransitionEdge t1;
    t1.source = "StateA";
    t1.target = "Target1";
    t1.event = "Ev";
    t1.guard = "x > 0";
    model.add_transition(t1);

    TransitionEdge t2;
    t2.source = "StateA";
    t2.target = "Target2";
    t2.event = "Ev";
    t2.guard = "x <= 0";
    model.add_transition(t2);

    // StateB transitions in reverse order: Target2 then Target1
    TransitionEdge t3;
    t3.source = "StateB";
    t3.target = "Target2";
    t3.event = "Ev";
    t3.guard = "x <= 0";
    model.add_transition(t3);

    TransitionEdge t4;
    t4.source = "StateB";
    t4.target = "Target1";
    t4.event = "Ev";
    t4.guard = "x > 0";
    model.add_transition(t4);

    StateMinimizationPass pass;
    DiagnosticEngine diag;
    bool ok = pass.run(model, diag);

    // StateA and StateB should be merged
    EXPECT_TRUE(ok);
    EXPECT_EQ(model.states.size(), 4u);
    EXPECT_NE(model.find_state("StateA"), nullptr);
    EXPECT_EQ(model.find_state("StateB"), nullptr);
}

/**
 * @brief Verify states with different do_activity or time invariants are kept separate.
 */
TEST(StateMinimization, ActivityAndInvariantDifferences_PreventMerger) {
    FsmIr model;
    model.name = "ActivityInvariantModel";
    model.add_state("Init");
    model.initial_state = "Init";
    model.add_state("Target");

    auto& s1 = model.add_state("StateWithActivity");
    s1.do_activity = "blink_led()";

    model.add_state("StateWithoutActivity");

    auto& s3 = model.add_state("StateWithInvariant");
    s3.time_invariant = StateTimeInvariant("stay_duration <= 500ms");

    for (const auto& s_name : {"StateWithActivity", "StateWithoutActivity", "StateWithInvariant"}) {
        TransitionEdge t;
        t.source = s_name;
        t.target = "Target";
        t.event = "Step";
        model.add_transition(t);
    }

    StateMinimizationPass pass;
    DiagnosticEngine diag;
    bool ok = pass.run(model, diag);

    EXPECT_FALSE(ok);
    EXPECT_EQ(model.states.size(), 5u);
}

}  // namespace


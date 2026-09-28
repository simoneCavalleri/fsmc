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

}  // namespace

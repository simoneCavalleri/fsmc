/**
 * @file test_dead_state_pruning.cpp
 * @brief Unit tests for DeadStatePruningPass reachability and dead transition analysis.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/passes/dead_state_pruning_pass.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend::passes;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify DeadStatePruningPass removes unreachable states and dead transitions.
 * @scenario FSM with reachable Init -> Active, an unreachable Island state, and a transition with guard == 'false'.
 * @expected Island state and false-guarded transition are pruned; only the reachable active graph remains.
 */
TEST(DeadStatePruning, UnreachableSubgraphsAndContradictoryGuards_EliminatedFromIr) {
    FsmIr ir;
    ir.name = "PruningFSM";
    ir.initial_state = "Init";

    ir.add_state("Init");
    ir.add_state("Active");
    ir.add_state("Island");  // Unreachable

    // Reachable transition
    ir.add_transition("Init", "Active", SignalTrigger{"Start", ""});

    // Dead transition (guard == false)
    TransitionEdge dead_t;
    dead_t.source = "Init";
    dead_t.target = "Active";
    dead_t.event = "NeverTrigger";
    dead_t.guard = "false";
    ir.add_transition(dead_t);

    // Transition from unreachable state
    ir.add_transition("Island", "Active", SignalTrigger{"FromIsland", ""});

    DeadStatePruningPass pass(true);
    DiagnosticEngine diag;
    pass.run(ir, diag);

    // Verify Island was pruned
    EXPECT_EQ(ir.states.size(), 2u);
    EXPECT_EQ(ir.find_state("Island"), nullptr);

    // Verify only the 1 reachable valid transition remains
    ASSERT_EQ(ir.transitions.size(), 1u);
    EXPECT_EQ(ir.transitions[0].event, "Start");
}

/**
 * @brief Verify DeadStatePruningPass does not prune enclosing parent states or global transitions
 *        when the initial state is configured to be a nested leaf state.
 */
TEST(DeadStatePruning, LeafInitialState_PreservesActiveParentAndGlobalTransitions) {
    FsmIr model;
    model.name = "HierarchicalPruningModel";
    model.initial_state = "LeafA";

    model.add_state("Operational");
    model.add_state("LeafA", "Operational");
    model.add_state("LeafB", "Operational");
    model.add_state("ErrorState");

    // Global abort transition from composite parent to ErrorState
    TransitionEdge t_err;
    t_err.source = "Operational";
    t_err.target = "ErrorState";
    t_err.event = "EvFault";
    model.add_transition(t_err);

    // Transition between leaves
    TransitionEdge t_step;
    t_step.source = "LeafA";
    t_step.target = "LeafB";
    t_step.event = "EvStep";
    model.add_transition(t_step);

    DiagnosticEngine diag;
    DeadStatePruningPass pass(true);
    pass.run(model, diag);

    // Operational and its targets must not be pruned
    EXPECT_NE(model.find_state("Operational"), nullptr);
    EXPECT_NE(model.find_state("ErrorState"), nullptr);
    EXPECT_NE(model.find_state("LeafA"), nullptr);
    EXPECT_NE(model.find_state("LeafB"), nullptr);

    // Both transitions must be preserved
    EXPECT_EQ(model.transitions.size(), 2u);
}

/**
 * @brief Verify that DeadStatePruningPass prunes unreachable choice nodes cleanly without leaving zombie choices.
 */
TEST(DeadStatePruning, UnreachableChoicePseudostate_PrunedCleanly) {
    FsmIr model;
    model.name = "UnreachChoiceModel";
    model.initial_state = "Idle";

    model.add_state("Idle");
    model.add_state("Active");
    model.add_choice_node("DeadChoice");

    // Reachable transition
    model.add_transition("Idle", "Active", SignalTrigger{"Go", ""});

    // Outgoing transitions from unreachable choice
    TransitionEdge t_choice1;
    t_choice1.source = "DeadChoice";
    t_choice1.target = "Active";
    t_choice1.guard = "x > 0";
    model.add_transition(t_choice1);

    TransitionEdge t_choice2;
    t_choice2.source = "DeadChoice";
    t_choice2.target = "Idle";
    t_choice2.guard = "else";
    model.add_transition(t_choice2);

    DiagnosticEngine diag;
    DeadStatePruningPass pass(true);
    pass.run(model, diag);

    // DeadChoice must be completely pruned from choice_nodes and states
    EXPECT_TRUE(model.choice_nodes.empty());
    EXPECT_EQ(model.find_state("DeadChoice"), nullptr);
    EXPECT_EQ(model.states.size(), 2u);
    EXPECT_EQ(model.transitions.size(), 1u);
    EXPECT_EQ(model.transitions[0].event, "Go");
}

/**
 * @brief Verify reachability when initial_state_id is distinct from initial_state name.
 */
TEST(DeadStatePruning, InitialStateIdDifferentFromName_PreservesReachableStates) {
    FsmIr model;
    model.name = "IdMismatchModel";
    model.initial_state = "Idle";
    model.initial_state_id = "_node_idle_guid_1";

    StateNode idle("Idle");
    idle.id = "_node_idle_guid_1";
    StateNode active("Active");
    active.id = "_node_active_guid_2";
    model.states.push_back(idle);
    model.states.push_back(active);

    TransitionEdge t;
    t.source = "Idle";
    t.target = "Active";
    t.source_id = "_node_idle_guid_1";
    t.target_id = "_node_active_guid_2";
    t.event = "EvStart";
    model.add_transition(t);

    DiagnosticEngine diag;
    DeadStatePruningPass pass(true);
    pass.run(model, diag);

    // Both states and transition must be preserved
    EXPECT_EQ(model.states.size(), 2u);
    EXPECT_NE(model.find_state("Idle"), nullptr);
    EXPECT_NE(model.find_state("Active"), nullptr);
    EXPECT_EQ(model.transitions.size(), 1u);
}

}  // namespace

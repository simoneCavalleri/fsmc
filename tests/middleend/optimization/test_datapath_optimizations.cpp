/**
 * @file test_datapath_optimizations.cpp
 * @brief Unit tests for Category B Data-Path Optimizations:
 *        DeadActionEliminationPass, RegisterLivenessPass, TransitionFusionPass, CommonActionFactoringPass.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/passes/common_action_factoring_pass.hpp"
#include "fsm/middleend/passes/dead_action_elimination_pass.hpp"
#include "fsm/middleend/passes/register_liveness_pass.hpp"
#include "fsm/middleend/passes/transition_fusion_pass.hpp"

using namespace fsm;
using namespace fsm::ir;
using namespace fsm::middleend::passes;
using namespace fsm::diagnostic;

// ============================================================================
// DeadActionEliminationPass Tests
// ============================================================================

/**
 * @brief Verify DeadActionEliminationPass prunes unread variable assignments.
 * @scenario Transition writes both 'used_var' and 'dead_var'; only 'used_var' is read by subsequent guards.
 * @expected Store operation to 'dead_var' is removed from transition actions, preserving store to 'used_var'.
 */
TEST(DeadActionElimination, UnreadVariableStore_PrunedFromTransitionAction) {
    FsmIr ir;
    ir.name = "DeadStoreMachine";
    ir.initial_state_id = "s1";

    ir.variables.emplace_back("used_var", DataType::int32(), "0");
    ir.variables.emplace_back("dead_var", DataType::int32(), "0");

    StateNode s1("s1", "s1");
    StateNode s2("s2", "s2");
    ir.states.push_back(s1);
    ir.states.push_back(s2);

    // Transition s1 -> s2 writes both used_var and dead_var
    TransitionEdge edge;
    edge.source_id = "s1";
    edge.target_id = "s2";
    edge.trigger = SignalTrigger("EV");

    ActionSignature act;
    StoreOp op_dead;
    op_dead.target = LValueTarget("dead_var");
    op_dead.expression = "99";
    act.instructions.emplace_back(op_dead);

    StoreOp op_used;
    op_used.target = LValueTarget("used_var");
    op_used.expression = "42";
    act.instructions.emplace_back(op_used);

    edge.transition_action = act;
    ir.add_transition(edge);

    // Transition s2 -> s1 uses used_var in guard
    TransitionEdge edge2;
    edge2.source_id = "s2";
    edge2.target_id = "s1";
    edge2.trigger = SignalTrigger("EV2");
    edge2.guard = "used_var > 10";
    edge2.guard_ast = GuardAstNode("used_var > 10");
    ir.add_transition(edge2);

    DiagnosticEngine diag;
    DeadActionEliminationPass pass;
    EXPECT_TRUE(pass.run(ir, diag));

    // dead_var store must have been pruned; used_var store preserved!
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    ASSERT_EQ(ir.transitions[0].transition_action->instructions.size(), 1);

    const auto& remaining = ir.transitions[0].transition_action->instructions[0];
    ASSERT_TRUE(std::holds_alternative<StoreOp>(remaining.op));
    EXPECT_EQ(std::get<StoreOp>(remaining.op).target.name, "used_var");
}

/**
 * @brief Verify DeadActionEliminationPass prunes write-after-write shadows and identity assignments.
 * @scenario Action sequence performs identity store 'x = x', overwritten store 'x = 10', and final store 'x = 20'.
 * @expected Identity and dead overwritten assignments are removed; only 'x = 20' remains in the action sequence.
 */
TEST(DeadActionElimination, OverwrittenAndIdentityStores_PrunedFromActionSequence) {
    FsmIr ir;
    ir.name = "WAWMachine";
    ir.initial_state_id = "s1";

    ir.variables.emplace_back("x", DataType::int32(), "0");

    StateNode s1("s1", "s1");
    ir.states.push_back(s1);

    TransitionEdge edge;
    edge.source_id = "s1";
    edge.target_id = "s1";
    edge.trigger = SignalTrigger("EV");

    ActionSignature act;
    // 1. x = x (identity assignment)
    StoreOp op_id;
    op_id.target = LValueTarget("x");
    op_id.expression = "x";
    act.instructions.emplace_back(op_id);

    // 2. x = 10 (overwritten by 3 without any read)
    StoreOp op_w1;
    op_w1.target = LValueTarget("x");
    op_w1.expression = "10";
    act.instructions.emplace_back(op_w1);

    // 3. x = 20
    StoreOp op_w2;
    op_w2.target = LValueTarget("x");
    op_w2.expression = "20";
    act.instructions.emplace_back(op_w2);

    edge.transition_action = act;
    ir.add_transition(edge);

    // Read x on another transition so x is not globally dead
    TransitionEdge edge2;
    edge2.source_id = "s1";
    edge2.target_id = "s1";
    edge2.trigger = SignalTrigger("CHECK");
    edge2.guard = "x == 20";
    edge2.guard_ast = GuardAstNode("x == 20");
    ir.add_transition(edge2);

    DiagnosticEngine diag;
    DeadActionEliminationPass pass;
    EXPECT_TRUE(pass.run(ir, diag));

    // Both identity (x = x) and dead write (x = 10) must be eliminated! Only x = 20 remains.
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    ASSERT_EQ(ir.transitions[0].transition_action->instructions.size(), 1);

    const auto& remaining = ir.transitions[0].transition_action->instructions[0];
    ASSERT_TRUE(std::holds_alternative<StoreOp>(remaining.op));
    EXPECT_EQ(std::get<StoreOp>(remaining.op).target.name, "x");
    EXPECT_EQ(std::get<StoreOp>(remaining.op).expression, "20");
}

// ============================================================================
// RegisterLivenessPass Tests
// ============================================================================

/**
 * @brief Verify RegisterLivenessPass shares hardware register allocations for variables with disjoint lifetimes.
 * @scenario Variable 'v1' is active only in phase 1 (s1 -> s2); variable 'v2' is active only in phase 2 (s2 -> s3).
 * @expected Both variables receive the identical hardware register index, demonstrating register reuse.
 */
TEST(RegisterLiveness, DisjointVariableLifetimes_SharesAllocatedRegisters) {
    FsmIr ir;
    ir.name = "RegAllocMachine";
    ir.initial_state_id = "s1";

    ir.variables.emplace_back("v1", DataType::int32(), "0");
    ir.variables.emplace_back("v2", DataType::int32(), "0");

    StateNode s1("s1", "s1");
    StateNode s2("s2", "s2");
    StateNode s3("s3", "s3");
    ir.states.push_back(s1);
    ir.states.push_back(s2);
    ir.states.push_back(s3);

    // Phase 1: s1 -> s2 writes v1
    TransitionEdge t1;
    t1.source_id = "s1";
    t1.target_id = "s2";
    t1.trigger = SignalTrigger("STEP1");
    ActionSignature a1;
    StoreOp op1;
    op1.target = LValueTarget("v1");
    op1.expression = "10";
    a1.instructions.emplace_back(op1);
    t1.transition_action = a1;
    ir.add_transition(t1);

    // s2 checks v1 and dies: transition s2 -> s3 writes v2, never reads v1 again
    TransitionEdge t2;
    t2.source_id = "s2";
    t2.target_id = "s3";
    t2.trigger = SignalTrigger("STEP2");
    t2.guard = "v1 == 10";
    t2.guard_ast = GuardAstNode("v1 == 10");
    ActionSignature a2;
    StoreOp op2;
    op2.target = LValueTarget("v2");
    op2.expression = "99";
    a2.instructions.emplace_back(op2);
    t2.transition_action = a2;
    ir.add_transition(t2);

    // s3 checks v2
    TransitionEdge t3;
    t3.source_id = "s3";
    t3.target_id = "s3";
    t3.trigger = SignalTrigger("CHECK");
    t3.guard = "v2 == 99";
    t3.guard_ast = GuardAstNode("v2 == 99");
    ir.add_transition(t3);

    DiagnosticEngine diag;
    RegisterLivenessPass pass;
    EXPECT_TRUE(pass.run(ir, diag));

    ASSERT_TRUE(ir.variables[0].register_index.has_value());
    ASSERT_TRUE(ir.variables[1].register_index.has_value());

    // Because v1 and v2 have completely disjoint active lifetimes across phases, they share the SAME hardware register!
    EXPECT_EQ(*ir.variables[0].register_index, *ir.variables[1].register_index);
}

/**
 * @brief Verify RegisterLivenessPass assigns distinct register allocations for simultaneously interfering variables.
 * @scenario Variables 'vx' and 'vy' are both evaluated simultaneously within the same transition guard expression.
 * @expected 'vx' and 'vy' are allocated different register indices due to lifetime interference.
 */
TEST(RegisterLiveness, InterferingVariableLifetimes_AllocatesDistinctRegisters) {
    FsmIr ir;
    ir.name = "InterferenceMachine";
    ir.initial_state_id = "s1";

    ir.variables.emplace_back("vx", DataType::int32(), "0");
    ir.variables.emplace_back("vy", DataType::int32(), "0");

    StateNode s1("s1", "s1");
    ir.states.push_back(s1);

    // Both vx and vy are read simultaneously in the same guard
    TransitionEdge t;
    t.source_id = "s1";
    t.target_id = "s1";
    t.trigger = SignalTrigger("COMPUTE");
    t.guard = "vx + vy > 10";
    t.guard_ast = GuardAstNode("vx + vy > 10");
    ir.add_transition(t);

    DiagnosticEngine diag;
    RegisterLivenessPass pass;
    EXPECT_TRUE(pass.run(ir, diag));

    ASSERT_TRUE(ir.variables[0].register_index.has_value());
    ASSERT_TRUE(ir.variables[1].register_index.has_value());

    // Because vx and vy interfere, they MUST receive different registers!
    EXPECT_NE(*ir.variables[0].register_index, *ir.variables[1].register_index);
}

// ============================================================================
// TransitionFusionPass Tests
// ============================================================================

/**
 * @brief Verify TransitionFusionPass fuses transient intermediate states and concatenates guards and actions.
 * @scenario State 's_start' transitions on 'EV1' to transient state 's_trans', which immediately transitions to
 * 's_end'.
 * @expected Transient state 's_trans' is eliminated; a direct fused transition connects 's_start' to 's_end'.
 */
TEST(TransitionFusion, TransientIntermediateState_FusesTransitionsAndBypassesState) {
    FsmIr ir;
    ir.name = "FusionMachine";
    ir.initial_state_id = "s_start";

    StateNode s_start("s_start", "s_start");
    StateNode s_trans("s_trans", "s_trans");
    StateNode s_end("s_end", "s_end");
    ir.states.push_back(s_start);
    ir.states.push_back(s_trans);
    ir.states.push_back(s_end);

    // Transition 1: s_start -> s_trans on event EV1 with guard G1 and action A1
    TransitionEdge t1;
    t1.source_id = "s_start";
    t1.target_id = "s_trans";
    t1.trigger = SignalTrigger("EV1");
    t1.guard = "x > 0";
    t1.guard_ast = GuardAstNode("x > 0");
    ActionSignature a1("act1");
    StoreOp op1;
    op1.target = LValueTarget("x");
    op1.expression = "x + 1";
    a1.instructions.emplace_back(op1);
    t1.transition_action = a1;
    ir.add_transition(t1);

    // Transition 2: s_trans -> s_end immediate (AnonymousTrigger) with guard G2 and action A2
    TransitionEdge t2;
    t2.source_id = "s_trans";
    t2.target_id = "s_end";
    t2.trigger = AnonymousTrigger{};
    t2.guard = "y < 10";
    t2.guard_ast = GuardAstNode("y < 10");
    ActionSignature a2("act2");
    StoreOp op2;
    op2.target = LValueTarget("y");
    op2.expression = "y * 2";
    a2.instructions.emplace_back(op2);
    t2.transition_action = a2;
    ir.add_transition(t2);

    DiagnosticEngine diag;
    TransitionFusionPass pass;
    EXPECT_TRUE(pass.run(ir, diag));

    // s_trans must have been removed!
    EXPECT_EQ(ir.states.size(), 2);
    EXPECT_EQ(ir.find_state("s_trans"), nullptr);

    // Fused transition must connect s_start directly to s_end
    ASSERT_EQ(ir.transitions.size(), 1);
    const auto& fused = ir.transitions[0];
    EXPECT_EQ(fused.source_id, "s_start");
    EXPECT_EQ(fused.target_id, "s_end");

    // Trigger must be EV1
    ASSERT_TRUE(std::holds_alternative<SignalTrigger>(fused.trigger));
    EXPECT_EQ(std::get<SignalTrigger>(fused.trigger).signal_name, "EV1");

    // Guard must be conjunction
    ASSERT_TRUE(fused.guard.has_value());
    EXPECT_TRUE(fused.guard->find("x > 0") != std::string::npos);
    EXPECT_TRUE(fused.guard->find("y < 10") != std::string::npos);

    // Action must chain both act1 and act2 instructions
    ASSERT_TRUE(fused.transition_action.has_value());
    EXPECT_EQ(fused.transition_action->instructions.size(), 2);
}

/**
 * @brief Verify TransitionFusionPass preserves states requiring external triggers or designated as initial states.
 * @scenario Two states with non-immediate event triggers ('EV1' and 'EV2').
 * @expected Neither state is fused or removed, maintaining explicit state machine topology.
 */
TEST(TransitionFusion, StatesWithExternalTriggersOrInitial_PreservedWithoutFusion) {
    FsmIr ir;
    ir.name = "NonTransientMachine";
    ir.initial_state_id = "s1";

    StateNode s1("s1", "s1");
    StateNode s2("s2", "s2");
    ir.states.push_back(s1);
    ir.states.push_back(s2);

    // s1 -> s2 with external event trigger EV
    TransitionEdge t1;
    t1.source_id = "s1";
    t1.target_id = "s2";
    t1.trigger = SignalTrigger("EV1");
    ir.add_transition(t1);

    // s2 -> s1 also has external event trigger EV2 (NOT immediate!)
    TransitionEdge t2;
    t2.source_id = "s2";
    t2.target_id = "s1";
    t2.trigger = SignalTrigger("EV2");
    ir.add_transition(t2);

    DiagnosticEngine diag;
    TransitionFusionPass pass;
    EXPECT_TRUE(pass.run(ir, diag));

    // Neither state should be removed!
    EXPECT_EQ(ir.states.size(), 2);
    EXPECT_EQ(ir.transitions.size(), 2);
}

// ============================================================================
// CommonActionFactoringPass Tests
// ============================================================================

/**
 * @brief Verify CommonActionFactoringPass factors common transition actions on convergent edges into target entry.
 * @scenario Convergent transitions from 's1' and 's2' to 'target' execute identical action 'counter = counter + 1'.
 * @expected Action is factored into target entry actions and stripped from the convergent transitions.
 */
TEST(CommonActionFactoring, ConvergentIncomingEdges_FactorsActionIntoTargetEntry) {
    FsmIr ir;
    ir.name = "ConvergentFactoringFSM";
    ir.initial_state_id = "init";

    StateNode init("init", "init");
    StateNode s1("s1", "s1");
    StateNode s2("s2", "s2");
    StateNode target("target", "target");

    ir.states.push_back(init);
    ir.states.push_back(s1);
    ir.states.push_back(s2);
    ir.states.push_back(target);

    // Initial transition to s1
    TransitionEdge t0;
    t0.source_id = "init";
    t0.target_id = "s1";
    ir.add_transition(t0);

    // Shared common action
    ActionSignature shared_act;
    StoreOp op;
    op.target = LValueTarget("counter");
    op.expression = "counter + 1";
    shared_act.instructions.emplace_back(op);

    // s1 -> target with shared_act
    TransitionEdge t1;
    t1.source_id = "s1";
    t1.target_id = "target";
    t1.trigger = SignalTrigger("EV1");
    t1.transition_action = shared_act;
    ir.add_transition(t1);

    // s2 -> target with shared_act
    TransitionEdge t2;
    t2.source_id = "s2";
    t2.target_id = "target";
    t2.trigger = SignalTrigger("EV2");
    t2.transition_action = shared_act;
    ir.add_transition(t2);

    DiagnosticEngine diag;
    CommonActionFactoringPass pass;
    EXPECT_TRUE(pass.run(ir, diag));

    // The shared action must now be factored into target's entry actions
    const auto* tgt = ir.find_state("target");
    ASSERT_NE(tgt, nullptr);
    ASSERT_EQ(tgt->entry_actions.size(), 1u);
    EXPECT_EQ(tgt->entry_actions[0].instructions.size(), 1u);

    // And removed from the convergent transitions
    EXPECT_FALSE(ir.transitions[1].transition_action.has_value());
    EXPECT_FALSE(ir.transitions[2].transition_action.has_value());
}

/**
 * @brief Verify CommonActionFactoringPass factors common transition actions on divergent edges into source exit.
 * @scenario Divergent transitions from 'src' to 't1' and 't2' execute identical action 'log_val = 100'.
 * @expected Action is factored into 'src' exit actions and stripped from outgoing transitions.
 */
TEST(CommonActionFactoring, DivergentOutgoingEdges_FactorsActionIntoSourceExit) {
    FsmIr ir;
    ir.name = "DivergentFactoringFSM";
    ir.initial_state_id = "src";

    StateNode src("src", "src");
    StateNode t1_st("t1", "t1");
    StateNode t2_st("t2", "t2");

    ir.states.push_back(src);
    ir.states.push_back(t1_st);
    ir.states.push_back(t2_st);

    ActionSignature shared_act;
    StoreOp op;
    op.target = LValueTarget("log_val");
    op.expression = "100";
    shared_act.instructions.emplace_back(op);

    // src -> t1 with shared_act
    TransitionEdge e1;
    e1.source_id = "src";
    e1.target_id = "t1";
    e1.trigger = SignalTrigger("EV_A");
    e1.transition_action = shared_act;
    ir.add_transition(e1);

    // src -> t2 with shared_act
    TransitionEdge e2;
    e2.source_id = "src";
    e2.target_id = "t2";
    e2.trigger = SignalTrigger("EV_B");
    e2.transition_action = shared_act;
    ir.add_transition(e2);

    DiagnosticEngine diag;
    CommonActionFactoringPass pass;
    EXPECT_TRUE(pass.run(ir, diag));

    // The shared action must now be factored into src's exit actions
    const auto* s = ir.find_state("src");
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->exit_actions.size(), 1u);
    EXPECT_EQ(s->exit_actions[0].instructions.size(), 1u);

    // And removed from the outgoing transitions
    EXPECT_FALSE(ir.transitions[0].transition_action.has_value());
    EXPECT_FALSE(ir.transitions[1].transition_action.has_value());
}

/**
 * @brief Verify CommonActionFactoringPass avoids hoisting into initial state entry actions to protect reset semantics.
 * @scenario Incoming transitions reset the machine to 'start' (initial state), executing action 'flag = 1'.
 * @expected Initial state entry actions are untouched; actions remain on transitions to prevent execution at power-on.
 */
TEST(CommonActionFactoring, InitialStateWithIncomingEdges_PreservesInitialStateSemantics) {
    FsmIr ir;
    ir.name = "InitialPreserveFSM";
    ir.initial_state_id = "start";

    StateNode start("start", "start");
    StateNode s1("s1", "s1");
    StateNode s2("s2", "s2");
    ir.states.push_back(start);
    ir.states.push_back(s1);
    ir.states.push_back(s2);

    ActionSignature act;
    StoreOp op;
    op.target = LValueTarget("flag");
    op.expression = "1";
    act.instructions.emplace_back(op);

    TransitionEdge e1;
    e1.source_id = "s1";
    e1.target_id = "start";
    e1.trigger = SignalTrigger("RESET1");
    e1.transition_action = act;
    ir.add_transition(e1);

    TransitionEdge e2;
    e2.source_id = "s2";
    e2.target_id = "start";
    e2.trigger = SignalTrigger("RESET2");
    e2.transition_action = act;
    ir.add_transition(e2);

    DiagnosticEngine diag;
    CommonActionFactoringPass pass;
    EXPECT_TRUE(pass.run(ir, diag));

    // Start state is the initial state: must NOT have action factored into entry actions!
    const auto* s = ir.find_state("start");
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->entry_actions.empty());
    EXPECT_TRUE(ir.transitions[0].transition_action.has_value());
    EXPECT_TRUE(ir.transitions[1].transition_action.has_value());
}

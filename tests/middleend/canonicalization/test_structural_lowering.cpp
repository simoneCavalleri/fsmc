/**
 * @file test_structural_lowering.cpp
 * @brief Unit tests for Category A Structural Lowering Suite:
 *        HistoryLoweringPass, DeferredEventLoweringPass, BoundaryActionFusionPass, ForkJoinLoweringPass.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/passes/boundary_action_fusion_pass.hpp"
#include "fsm/middleend/passes/connective_junction_chaining_pass.hpp"
#include "fsm/middleend/passes/deferred_event_lowering_pass.hpp"
#include "fsm/middleend/passes/fork_join_lowering_pass.hpp"
#include "fsm/middleend/passes/history_lowering_pass.hpp"
#include "fsm/middleend/passes/sampled_change_trigger_pass.hpp"

using namespace fsm::ir;
using namespace fsm::diagnostic;
using namespace fsm::middleend::passes;

// ============================================================================
// 1. HistoryLoweringPass Tests
// ============================================================================

/**
 * @brief Verify shallow history pseudostate lowering into shadow variables and dynamic dispatch.
 * @scenario Composite state 'Operating' containing shallow history pseudostate '[H]' and sub-states 'ModeA' and
 * 'ModeB'.
 * @expected History pseudostate is removed, shadow state variable is synthesized, and restore choice node is wired.
 */
TEST(HistoryLowering, ShallowHistoryTarget_LowersToShadowRegisterAndDispatchGuards) {
    FsmIr ir;
    DiagnosticEngine diag;

    StateNode comp("Operating", "Composite parent");
    comp.is_composite = true;
    comp.has_history = true;
    comp.initial_sub_state = "ModeA";

    StateNode subA("ModeA", "Substate A", "Operating");
    StateNode subB("ModeB", "Substate B", "Operating");
    StateNode hist("[H]", "Shallow history", "Operating");
    hist.kind = StateKind::ShallowHistory;

    ir.states.push_back(comp);
    ir.states.push_back(subA);
    ir.states.push_back(subB);
    ir.states.push_back(hist);

    // Incoming transition to history
    TransitionEdge t_enter("Idle", "Operating", "EvResume");
    t_enter.target_is_history = true;
    ir.transitions.push_back(t_enter);

    bool res = HistoryLoweringPass::run(ir, diag);
    EXPECT_TRUE(res);

    // 1. History pseudostate pruned
    EXPECT_EQ(ir.find_state("[H]"), nullptr);

    // 2. Shadow history register synthesized
    const auto* hvar = ir.find_variable("__history_state_Operating");
    ASSERT_NE(hvar, nullptr);
    EXPECT_EQ(hvar->type, "string");

    // 3. Substates have exit actions recording history
    const auto* subA_after = ir.find_state("ModeA");
    ASSERT_NE(subA_after, nullptr);
    EXPECT_FALSE(subA_after->exit_actions.empty());

    // 4. Restore choice pseudostate created
    const auto* choice_node = ir.find_state("Operating_HistoryRestore");
    ASSERT_NE(choice_node, nullptr);
    EXPECT_EQ(choice_node->kind, StateKind::Choice);

    // 5. Incoming transition retargeted to choice node
    EXPECT_EQ(ir.transitions[0].target, "Operating_HistoryRestore");
    EXPECT_FALSE(ir.transitions[0].target_is_history);
}

// ============================================================================
// 2. DeferredEventLoweringPass Tests
// ============================================================================

/**
 * @brief Verify deferred events lowering into bounded queue buffers and recall transitions.
 * @scenario State 'Busy' defers 'EvSensorData' and 'EvTelemetry' before transitioning to 'Idle' on 'EvFinish'.
 * @expected Deferred event list is cleared from state, queue variables synthesized, and self-buffering transitions
 * added.
 */
TEST(DeferredEventLowering, DeferredEventsDeclared_LowersToBufferVariablesAndRecallTransitions) {
    FsmIr ir;
    DiagnosticEngine diag;

    StateNode busy("Busy", "State deferring events");
    busy.deferred_events.push_back("EvSensorData");
    busy.deferred_events.push_back("EvTelemetry");

    StateNode idle("Idle", "Normal state");

    ir.states.push_back(busy);
    ir.states.push_back(idle);

    TransitionEdge t_exit("Busy", "Idle", "EvFinish");
    ir.transitions.push_back(t_exit);

    bool res = DeferredEventLoweringPass::run(ir, diag);
    EXPECT_TRUE(res);

    // 1. Deferred events cleared from state node
    const auto* busy_after = ir.find_state("Busy");
    ASSERT_NE(busy_after, nullptr);
    EXPECT_TRUE(busy_after->deferred_events.empty());

    // 2. Bounded buffer variable synthesized
    EXPECT_NE(ir.find_variable("__deferred_count"), nullptr);
    EXPECT_NE(ir.find_variable("__deferred_buffer"), nullptr);

    // 3. Internal self-transitions buffering events synthesized
    bool found_sensor_trans = false;
    bool found_telemetry_trans = false;
    for (const auto& t : ir.transitions) {
        if (t.source == "Busy" && t.target == "Busy") {
            if (t.event == "EvSensorData")
                found_sensor_trans = true;
            if (t.event == "EvTelemetry")
                found_telemetry_trans = true;
        }
    }
    EXPECT_TRUE(found_sensor_trans);
    EXPECT_TRUE(found_telemetry_trans);

    // 4. Exit transition has recall action attached
    bool exit_has_recall = false;
    for (const auto& t : ir.transitions) {
        if (t.source == "Busy" && t.target == "Idle") {
            if (t.transition_action.has_value())
                exit_has_recall = true;
        }
    }
    EXPECT_TRUE(exit_has_recall);
}

// ============================================================================
// 3. BoundaryActionFusionPass Tests
// ============================================================================

/**
 * @brief Verify boundary action fusion concatenates exit and entry actions in Lowest Common Ancestor (LCA) order.
 * @scenario Cross-hierarchy transition from 'Root::CompositeA::SubA' to 'Root::CompositeB::SubB' with transition
 * action.
 * @expected Action instructions fused in strict order: exit(SubA), exit(CompositeA), transition_action,
 * entry(CompositeB), entry(SubB).
 */
TEST(BoundaryActionFusion, CrossBoundaryTransition_FusesExitAndEntryActionsInLcaOrder) {
    FsmIr ir;
    DiagnosticEngine diag;

    // Hierarchy:
    // Root -> CompositeA -> SubA
    // Root -> CompositeB -> SubB
    StateNode root("Root");
    root.is_composite = true;

    StateNode compA("CompositeA", "", "Root");
    compA.is_composite = true;
    ActionSignature compA_exit("exit_CompositeA");
    StoreOp op_a;
    op_a.expression = "exit_A";
    compA_exit.instructions.emplace_back(op_a);
    compA.exit_actions.push_back(compA_exit);

    StateNode subA("SubA", "", "CompositeA");
    ActionSignature subA_exit("exit_SubA");
    StoreOp op_suba;
    op_suba.expression = "exit_SubA";
    subA_exit.instructions.emplace_back(op_suba);
    subA.exit_actions.push_back(subA_exit);

    StateNode compB("CompositeB", "", "Root");
    compB.is_composite = true;
    ActionSignature compB_entry("entry_CompositeB");
    StoreOp op_b;
    op_b.expression = "entry_B";
    compB_entry.instructions.emplace_back(op_b);
    compB.entry_actions.push_back(compB_entry);

    StateNode subB("SubB", "", "CompositeB");
    ActionSignature subB_entry("entry_SubB");
    StoreOp op_subb;
    op_subb.expression = "entry_SubB";
    subB_entry.instructions.emplace_back(op_subb);
    subB.entry_actions.push_back(subB_entry);

    ir.states.push_back(root);
    ir.states.push_back(compA);
    ir.states.push_back(subA);
    ir.states.push_back(compB);
    ir.states.push_back(subB);

    // Transition from SubA to SubB with transition action
    ActionSignature trans_act("mid_trans");
    StoreOp op_mid;
    op_mid.expression = "mid_action";
    trans_act.instructions.emplace_back(op_mid);

    TransitionEdge cross_edge("SubA", "SubB", "EvJump", std::nullopt, trans_act);
    ir.transitions.push_back(cross_edge);

    bool res = BoundaryActionFusionPass::run(ir, diag);
    EXPECT_TRUE(res);

    // Assert that the transition action has exactly 5 fused instructions in order:
    // [exit(SubA), exit(CompositeA), mid_action, entry(CompositeB), entry(SubB)]
    const auto& t = ir.transitions[0];
    ASSERT_TRUE(t.transition_action.has_value());
    const auto& insts = t.transition_action->instructions;
    ASSERT_EQ(insts.size(), 5U);

    EXPECT_EQ(std::get<StoreOp>(insts[0].op).expression, "exit_SubA");
    EXPECT_EQ(std::get<StoreOp>(insts[1].op).expression, "exit_A");
    EXPECT_EQ(std::get<StoreOp>(insts[2].op).expression, "mid_action");
    EXPECT_EQ(std::get<StoreOp>(insts[3].op).expression, "entry_B");
    EXPECT_EQ(std::get<StoreOp>(insts[4].op).expression, "entry_SubB");
}

/**
 * @brief Verify that BoundaryActionFusionPass clears entry/exit hooks on traversed StateNodes.
 * @scenario Same cross-hierarchy model as the fusion order test.
 * @expected After the pass runs, exit_actions on SubA and CompositeA are empty, and
 *           entry_actions on CompositeB and SubB are empty — preventing double execution
 *           in the generated C++ runtime lifecycle callbacks.
 */
TEST(BoundaryActionFusion, CrossBoundaryTransition_ClearsHooksOnFusedNodes) {
    FsmIr ir;
    DiagnosticEngine diag;

    StateNode root("Root");
    root.is_composite = true;

    StateNode compA("CompositeA", "", "Root");
    compA.is_composite = true;
    ActionSignature compA_exit("exit_CompositeA");
    StoreOp op_a;
    op_a.expression = "exit_A";
    compA_exit.instructions.emplace_back(op_a);
    compA.exit_actions.push_back(compA_exit);

    StateNode subA("SubA", "", "CompositeA");
    ActionSignature subA_exit("exit_SubA");
    StoreOp op_suba;
    op_suba.expression = "exit_SubA";
    subA_exit.instructions.emplace_back(op_suba);
    subA.exit_actions.push_back(subA_exit);

    StateNode compB("CompositeB", "", "Root");
    compB.is_composite = true;
    ActionSignature compB_entry("entry_CompositeB");
    StoreOp op_b;
    op_b.expression = "entry_B";
    compB_entry.instructions.emplace_back(op_b);
    compB.entry_actions.push_back(compB_entry);

    StateNode subB("SubB", "", "CompositeB");
    ActionSignature subB_entry("entry_SubB");
    StoreOp op_subb;
    op_subb.expression = "entry_SubB";
    subB_entry.instructions.emplace_back(op_subb);
    subB.entry_actions.push_back(subB_entry);

    ir.states.push_back(root);
    ir.states.push_back(compA);
    ir.states.push_back(subA);
    ir.states.push_back(compB);
    ir.states.push_back(subB);

    TransitionEdge cross_edge("SubA", "SubB", "EvJump");
    ir.transitions.push_back(cross_edge);

    bool res = BoundaryActionFusionPass::run(ir, diag);
    ASSERT_TRUE(res);

    // After fusion the hooks on traversed nodes must be cleared.
    const auto* subA_after = ir.find_state("SubA");
    const auto* compA_after = ir.find_state("CompositeA");
    const auto* compB_after = ir.find_state("CompositeB");
    const auto* subB_after = ir.find_state("SubB");
    ASSERT_NE(subA_after, nullptr);
    ASSERT_NE(compA_after, nullptr);
    ASSERT_NE(compB_after, nullptr);
    ASSERT_NE(subB_after, nullptr);

    EXPECT_TRUE(subA_after->exit_actions.empty())
        << "SubA exit_actions must be cleared after fusion (double-execution prevention)";
    EXPECT_TRUE(compA_after->exit_actions.empty())
        << "CompositeA exit_actions must be cleared after fusion (double-execution prevention)";
    EXPECT_TRUE(compB_after->entry_actions.empty())
        << "CompositeB entry_actions must be cleared after fusion (double-execution prevention)";
    EXPECT_TRUE(subB_after->entry_actions.empty())
        << "SubB entry_actions must be cleared after fusion (double-execution prevention)";

    // Root node is the LCA and must NOT be touched.
    const auto* root_after = ir.find_state("Root");
    ASSERT_NE(root_after, nullptr);
    EXPECT_TRUE(root_after->entry_actions.empty()) << "Root (LCA) must not be modified";
    EXPECT_TRUE(root_after->exit_actions.empty()) << "Root (LCA) must not be modified";
}

/**
 * @brief Verify that BoundaryActionFusionPass does not modify internal transitions.
 * @scenario State 'A' has both an internal self-transition (on EvInternal) and an exit action.
 * @expected The internal transition is left unchanged; no fusion is attempted on it.
 */
TEST(BoundaryActionFusion, InternalTransition_NotModifiedByPass) {
    FsmIr ir;
    DiagnosticEngine diag;

    StateNode state_a("A");
    ActionSignature exit_act("exit_A");
    StoreOp exit_op;
    exit_op.expression = "side_effect";
    exit_act.instructions.emplace_back(exit_op);
    state_a.exit_actions.push_back(exit_act);

    StateNode state_b("B");

    ir.states.push_back(state_a);
    ir.states.push_back(state_b);

    // Internal self-transition on A: must NOT be fused.
    TransitionEdge internal_t("A", "A", "EvInternal");
    internal_t.kind = TransitionEdgeKind::Internal;
    ir.transitions.push_back(internal_t);

    bool res = BoundaryActionFusionPass::run(ir, diag);

    // The pass returns false: no external transition with boundary actions was found.
    EXPECT_FALSE(res) << "No external cross-boundary transition exists; pass must be a no-op";

    // Internal transition must be unchanged.
    ASSERT_EQ(ir.transitions.size(), 1U);
    EXPECT_EQ(ir.transitions[0].kind, TransitionEdgeKind::Internal);
    EXPECT_FALSE(ir.transitions[0].transition_action.has_value()) << "Internal transition must not gain a fused action";

    // Exit action on A must NOT have been cleared.
    const auto* a_after = ir.find_state("A");
    ASSERT_NE(a_after, nullptr);
    EXPECT_FALSE(a_after->exit_actions.empty())
        << "State A exit_actions must not be cleared when only an internal transition exists";
}

/**
 * @brief Verify that multiple outgoing transitions from the same state both fuse boundary actions.
 * @scenario Composite state CompositeA has exit actions and substate SubA has exit actions.
 *           Two distinct outgoing transitions leave SubA to SubB1 and SubB2.
 * @expected Both transitions fuse the exit actions without premature clearing on the first transition.
 */
TEST(BoundaryActionFusion, MultipleOutgoingTransitions_BothFuseBoundaryActionsWithoutPrematureClearing) {
    FsmIr ir;
    DiagnosticEngine diag;

    StateNode root("Root");
    root.is_composite = true;

    StateNode compA("CompositeA", "", "Root");
    compA.is_composite = true;
    ActionSignature compA_exit("exit_CompositeA");
    StoreOp op_a;
    op_a.expression = "exit_A";
    compA_exit.instructions.emplace_back(op_a);
    compA.exit_actions.push_back(compA_exit);

    StateNode subA("SubA", "", "CompositeA");
    ActionSignature subA_exit("exit_SubA");
    StoreOp op_suba;
    op_suba.expression = "exit_SubA";
    subA_exit.instructions.emplace_back(op_suba);
    subA.exit_actions.push_back(subA_exit);

    StateNode compB("CompositeB", "", "Root");
    compB.is_composite = true;

    StateNode subB1("SubB1", "", "CompositeB");
    StateNode subB2("SubB2", "", "CompositeB");

    ir.states.push_back(root);
    ir.states.push_back(compA);
    ir.states.push_back(subA);
    ir.states.push_back(compB);
    ir.states.push_back(subB1);
    ir.states.push_back(subB2);

    TransitionEdge t1("SubA", "SubB1", "EvJump1");
    TransitionEdge t2("SubA", "SubB2", "EvJump2");
    ir.transitions.push_back(t1);
    ir.transitions.push_back(t2);

    bool res = BoundaryActionFusionPass::run(ir, diag);
    EXPECT_TRUE(res);

    ASSERT_EQ(ir.transitions.size(), 2U);
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    ASSERT_TRUE(ir.transitions[1].transition_action.has_value());

    EXPECT_EQ(ir.transitions[0].transition_action->instructions.size(), 2U);
    EXPECT_EQ(ir.transitions[1].transition_action->instructions.size(), 2U);

    EXPECT_EQ(std::get<StoreOp>(ir.transitions[0].transition_action->instructions[0].op).expression, "exit_SubA");
    EXPECT_EQ(std::get<StoreOp>(ir.transitions[0].transition_action->instructions[1].op).expression, "exit_A");

    EXPECT_EQ(std::get<StoreOp>(ir.transitions[1].transition_action->instructions[0].op).expression, "exit_SubA");
    EXPECT_EQ(std::get<StoreOp>(ir.transitions[1].transition_action->instructions[1].op).expression, "exit_A");
}

// ============================================================================
// 4. ForkJoinLoweringPass Tests
// ============================================================================

/**
 * @brief Verify fork and join pseudostates lowering into multi-target and multi-source transition edges.
 * @scenario State 'Init' transitions through Fork1 to parallel regions RegionA and RegionB; regions join at Join1 to
 * 'Done'.
 * @expected Fork and Join nodes are pruned; single multi-target and multi-source transition edges are synthesized.
 */
TEST(ForkJoinLowering, ForkAndJoinPseudostates_LowersToMultiSourceMultiTargetTransitions) {
    FsmIr ir;
    DiagnosticEngine diag;

    StateNode init("Init");
    StateNode fork_node("Fork1");
    fork_node.kind = StateKind::Fork;
    StateNode regA("RegionA");
    StateNode regB("RegionB");
    StateNode join_node("Join1");
    join_node.kind = StateKind::Join;
    StateNode done("Done");

    ir.states.push_back(init);
    ir.states.push_back(fork_node);
    ir.states.push_back(regA);
    ir.states.push_back(regB);
    ir.states.push_back(join_node);
    ir.states.push_back(done);

    // Fork edges: Init -> Fork1, Fork1 -> RegionA, Fork1 -> RegionB
    ir.transitions.emplace_back("Init", "Fork1", "EvFork");
    ir.transitions.emplace_back("Fork1", "RegionA", "");
    ir.transitions.emplace_back("Fork1", "RegionB", "");

    // Join edges: RegionA -> Join1, RegionB -> Join1, Join1 -> Done
    ir.transitions.emplace_back("RegionA", "Join1", "EvA");
    ir.transitions.emplace_back("RegionB", "Join1", "EvB");
    ir.transitions.emplace_back("Join1", "Done", "");

    bool res = ForkJoinLoweringPass::run(ir, diag);
    EXPECT_TRUE(res);

    // 1. Fork and Join state nodes pruned
    EXPECT_EQ(ir.find_state("Fork1"), nullptr);
    EXPECT_EQ(ir.find_state("Join1"), nullptr);

    // 2. Incoming transition to fork now has 2 targets
    const TransitionEdge* fork_in = nullptr;
    for (const auto& t : ir.transitions) {
        if (t.source == "Init")
            fork_in = &t;
    }
    ASSERT_NE(fork_in, nullptr);
    EXPECT_EQ(fork_in->target_ids.size(), 2U);
    EXPECT_EQ(fork_in->multi_target_ids.size(), 2U);

    // 3. Outgoing transition from join now has 2 sources
    const TransitionEdge* join_out = nullptr;
    for (const auto& t : ir.transitions) {
        if (t.target == "Done")
            join_out = &t;
    }
    ASSERT_NE(join_out, nullptr);
    EXPECT_EQ(join_out->source_ids.size(), 2U);
    EXPECT_EQ(join_out->multi_source_ids.size(), 2U);
}

// ============================================================================
// 5. SampledChangeTriggerPass Tests
// ============================================================================

/**
 * @brief Verify continuous ChangeTrigger lowering into sampled edge detector.
 * @scenario State 'Monitoring' with transition 'Monitoring -> Alarm' on 'when(temp > 100)'.
 * @expected Shadow register '__change_Monitoring_0_prev' allocated, entry action added to Monitoring,
 *           guard synthesized to '(!__change_Monitoring_0_prev && (temp > 100))', update action added,
 *           trigger lowered to AnonymousTrigger.
 */
TEST(SampledChangeTrigger, RisingEdgeTrigger_LowersToShadowRegisterAndEdgeGuard) {
    FsmIr ir;
    DiagnosticEngine diag;

    ir.add_state("Monitoring");
    ir.add_state("Alarm");

    TransitionEdge t("Monitoring", "Alarm", "");
    t.trigger = ChangeTrigger("temp > 100", true);
    ir.transitions.push_back(t);

    SampledChangeTriggerPass pass;
    bool res = pass.run(ir, diag);
    EXPECT_TRUE(res);

    // 1. Trigger lowered to AnonymousTrigger
    ASSERT_EQ(ir.transitions.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<AnonymousTrigger>(ir.transitions[0].trigger));

    // 2. Guard contains edge detection
    ASSERT_TRUE(ir.transitions[0].guard.has_value());
    EXPECT_NE(ir.transitions[0].guard->find("(!__change_Monitoring_0_prev && (temp > 100))"), std::string::npos);

    // 3. Shadow register allocated in variables
    const auto* shadow = ir.find_variable("__change_Monitoring_0_prev");
    ASSERT_NE(shadow, nullptr);
    EXPECT_EQ(shadow->initial_value, "false");

    // 4. Source state entry actions capture predicate state
    const auto* src = ir.find_state("Monitoring");
    ASSERT_NE(src, nullptr);
    ASSERT_FALSE(src->entry_actions.empty());
    EXPECT_EQ(src->entry_actions[0].assignments[0].target.full_path(), "__change_Monitoring_0_prev");
    EXPECT_EQ(src->entry_actions[0].assignments[0].expression, "temp > 100");

    // 5. Transition action updates shadow register
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    EXPECT_EQ(ir.transitions[0].transition_action->assignments[0].target.full_path(), "__change_Monitoring_0_prev");
}

TEST(SampledChangeTrigger, FallingEdgeTrigger_SynthesizesHighToLowDetector) {
    FsmIr ir;
    DiagnosticEngine diag;

    ir.add_state("Running");
    ir.add_state("Stopped");

    TransitionEdge t("Running", "Stopped", "");
    t.trigger = ChangeTrigger("is_active", false);  // active_on_true = false
    ir.transitions.push_back(t);

    SampledChangeTriggerPass pass;
    bool res = pass.run(ir, diag);
    EXPECT_TRUE(res);

    ASSERT_TRUE(ir.transitions[0].guard.has_value());
    EXPECT_NE(ir.transitions[0].guard->find("(__change_Running_0_prev && !(is_active))"), std::string::npos);
}

TEST(SampledChangeTrigger, NoChangeTriggers_ReturnsFalse) {
    FsmIr ir;
    DiagnosticEngine diag;

    ir.add_state("S1");
    ir.add_state("S2");
    ir.transitions.push_back(TransitionEdge("S1", "S2", "EvStep"));

    SampledChangeTriggerPass pass;
    bool res = pass.run(ir, diag);
    EXPECT_FALSE(res);
}

// ============================================================================
// 6. ConnectiveJunctionChainingPass Tests
// ============================================================================

TEST(ConnectiveJunctionChaining, MultiHopChain_FusesIntoCompoundTransition) {
    FsmIr ir;
    DiagnosticEngine diag;

    ir.add_state("StateA");
    ir.add_state("J1", "", StateKind::Junction);
    ir.add_state("J2", "", StateKind::Junction);
    ir.add_state("StateB");

    // Segment 1: StateA -> J1 on EvTick
    TransitionEdge t1("StateA", "J1", "EvTick");
    t1.condition_action = ActionSignature("ca1");
    ir.transitions.push_back(t1);

    // Segment 2: J1 -> J2 [x > 0]
    TransitionEdge t2("J1", "J2", "", "x > 0");
    t2.condition_action = ActionSignature("ca2");
    ir.transitions.push_back(t2);

    // Segment 3: J2 -> StateB [y > 0] / { ta1 }
    TransitionEdge t3("J2", "StateB", "", "y > 0", ActionSignature("ta1"));
    ir.transitions.push_back(t3);

    bool res = ConnectiveJunctionChainingPass::run(ir, diag);
    EXPECT_TRUE(res);
    EXPECT_FALSE(diag.has_errors());

    // Junctions J1 and J2 removed
    EXPECT_EQ(ir.find_state("J1"), nullptr);
    EXPECT_EQ(ir.find_state("J2"), nullptr);
    EXPECT_EQ(ir.states.size(), 2u);

    // Intermediate transitions removed, single compound transition created
    ASSERT_EQ(ir.transitions.size(), 1u);
    const auto& comp = ir.transitions[0];
    EXPECT_EQ(comp.source, "StateA");
    EXPECT_EQ(comp.target, "StateB");
    EXPECT_EQ(comp.event, "EvTick");
    ASSERT_TRUE(comp.guard.has_value());
    EXPECT_EQ(*comp.guard, "fsm::and_<x > 0, y > 0>");
    ASSERT_TRUE(comp.condition_action.has_value());
    EXPECT_EQ(comp.condition_action->name, "ca1_ca2");
    ASSERT_TRUE(comp.transition_action.has_value());
    EXPECT_EQ(comp.transition_action->name, "ta1");
}

TEST(ConnectiveJunctionChaining, BranchingJunctions_CreatesMultipleCompositeTransitions) {
    FsmIr ir;
    DiagnosticEngine diag;

    ir.add_state("Start");
    ir.add_state("JuncBranch", "", StateKind::Junction);
    ir.add_state("BranchSuccess");
    ir.add_state("BranchFail");

    TransitionEdge t_in("Start", "JuncBranch", "EvCheck");
    TransitionEdge t_ok("JuncBranch", "BranchSuccess", "", "status == 0");
    TransitionEdge t_err("JuncBranch", "BranchFail", "", "status != 0");

    ir.transitions.push_back(t_in);
    ir.transitions.push_back(t_ok);
    ir.transitions.push_back(t_err);

    bool res = ConnectiveJunctionChainingPass::run(ir, diag);
    EXPECT_TRUE(res);
    EXPECT_FALSE(diag.has_errors());

    EXPECT_EQ(ir.find_state("JuncBranch"), nullptr);
    ASSERT_EQ(ir.transitions.size(), 2u);

    bool found_ok = false;
    bool found_err = false;
    for (const auto& t : ir.transitions) {
        EXPECT_EQ(t.source, "Start");
        EXPECT_EQ(t.event, "EvCheck");
        if (t.target == "BranchSuccess") {
            found_ok = true;
            EXPECT_EQ(t.guard.value_or(""), "status == 0");
        } else if (t.target == "BranchFail") {
            found_err = true;
            EXPECT_EQ(t.guard.value_or(""), "status != 0");
        }
    }
    EXPECT_TRUE(found_ok);
    EXPECT_TRUE(found_err);
}

TEST(ConnectiveJunctionChaining, CyclicJunctions_ReportsDiagnosticError) {
    FsmIr ir;
    DiagnosticEngine diag;

    ir.add_state("StateA");
    ir.add_state("J1", "", StateKind::Junction);
    ir.add_state("J2", "", StateKind::Junction);
    ir.add_state("StateB");

    ir.transitions.push_back(TransitionEdge("StateA", "J1", "Ev"));
    ir.transitions.push_back(TransitionEdge("J1", "J2", ""));
    ir.transitions.push_back(TransitionEdge("J2", "J1", ""));  // Cycle!
    ir.transitions.push_back(TransitionEdge("J2", "StateB", ""));

    bool res = ConnectiveJunctionChainingPass::run(ir, diag);
    EXPECT_FALSE(res);
    EXPECT_TRUE(diag.has_errors());
    EXPECT_EQ(diag.get_diagnostics()[0].code, "EJUNC001");
}

/**
 * @brief Verify that a self-transition chained through a junction preserves External kind.
 * @scenario StateA transitions to Junc, which transitions back to StateA on [flag == true].
 * @expected Chained transition StateA -> StateA has TransitionEdgeKind::External so lifecycle hooks fire.
 */
TEST(ConnectiveJunctionChaining, SelfTransitionThroughJunction_PreservesExternalKind) {
    FsmIr ir;
    DiagnosticEngine diag;

    ir.add_state("StateA");
    ir.add_state("JuncSelf", "", StateKind::Junction);

    TransitionEdge t_in("StateA", "JuncSelf", "EvLoop");
    TransitionEdge t_out("JuncSelf", "StateA", "", "flag == true");
    ir.transitions.push_back(t_in);
    ir.transitions.push_back(t_out);

    bool res = ConnectiveJunctionChainingPass::run(ir, diag);
    EXPECT_TRUE(res);
    EXPECT_FALSE(diag.has_errors());

    ASSERT_EQ(ir.transitions.size(), 1u);
    const auto& comp = ir.transitions[0];
    EXPECT_EQ(comp.source, "StateA");
    EXPECT_EQ(comp.target, "StateA");
    EXPECT_EQ(comp.event, "EvLoop");
    EXPECT_EQ(comp.guard.value_or(""), "flag == true");
    EXPECT_EQ(comp.kind, TransitionEdgeKind::External);
}

/**
 * @file test_common_action_factoring.cpp
 * @brief Unit tests for CommonActionFactoringPass redundant action hoist and sink optimizations.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/passes/common_action_factoring_pass.hpp"

using namespace fsm;
using namespace fsm::ir;
using namespace fsm::middleend::passes;
using namespace fsm::diagnostic;

/**
 * @brief Verify factoring of common transition actions on convergent edges into target state entry actions.
 * @scenario Three distinct states (s1, s2, s3) transition into 'target' state, each executing identical store operation
 * 'status = READY'.
 * @expected Identical transition actions are hoisted from the 3 edges and factored once into 'target' entry actions.
 */
TEST(CommonActionFactoring, ConvergentTransitionsIdenticalAction_FactoredIntoTargetEntry) {
    FsmIr ir;
    ir.name = "ConvergentTest";
    ir.initial_state_id = "start";

    StateNode start("start", "start");
    StateNode s1("s1", "s1");
    StateNode s2("s2", "s2");
    StateNode s3("s3", "s3");
    StateNode target("target", "target");

    ir.states.push_back(start);
    ir.states.push_back(s1);
    ir.states.push_back(s2);
    ir.states.push_back(s3);
    ir.states.push_back(target);

    ActionSignature common_act;
    StoreOp store_op;
    store_op.target = LValueTarget("status");
    store_op.expression = "READY";
    common_act.instructions.emplace_back(store_op);

    // Three transitions converging onto target with the same action
    TransitionEdge t1;
    t1.source_id = "s1";
    t1.target_id = "target";
    t1.trigger = SignalTrigger("EV1");
    t1.transition_action = common_act;
    ir.add_transition(t1);

    TransitionEdge t2;
    t2.source_id = "s2";
    t2.target_id = "target";
    t2.trigger = SignalTrigger("EV2");
    t2.transition_action = common_act;
    ir.add_transition(t2);

    TransitionEdge t3;
    t3.source_id = "s3";
    t3.target_id = "target";
    t3.trigger = SignalTrigger("EV3");
    t3.transition_action = common_act;
    ir.add_transition(t3);

    DiagnosticEngine diag;
    CommonActionFactoringPass pass;
    EXPECT_TRUE(pass.run(ir, diag));

    // Common action must be factored into target entry actions
    const auto* tgt = ir.find_state("target");
    ASSERT_NE(tgt, nullptr);
    ASSERT_EQ(tgt->entry_actions.size(), 1u);
    EXPECT_EQ(tgt->entry_actions[0].instructions.size(), 1u);

    // And removed from all 3 transitions
    for (const auto& t : ir.transitions) {
        EXPECT_FALSE(t.transition_action.has_value());
    }
}

/**
 * @brief Verify factoring of common transition actions on divergent edges into source state exit actions.
 * @scenario State 'hub' transitions to multiple branches (b1, b2), each performing identical assignment 'led = OFF'.
 * @expected Common action is removed from outgoing transitions and factored once into 'hub' exit actions.
 */
TEST(CommonActionFactoring, DivergentTransitionsIdenticalAction_FactoredIntoSourceExit) {
    FsmIr ir;
    ir.name = "DivergentTest";
    ir.initial_state_id = "init";

    StateNode init("init", "init");
    StateNode hub("hub", "hub");
    StateNode b1("b1", "b1");
    StateNode b2("b2", "b2");

    ir.states.push_back(init);
    ir.states.push_back(hub);
    ir.states.push_back(b1);
    ir.states.push_back(b2);

    ActionSignature common_act;
    StoreOp store_op;
    store_op.target = LValueTarget("led");
    store_op.expression = "OFF";
    common_act.instructions.emplace_back(store_op);

    TransitionEdge e1;
    e1.source_id = "hub";
    e1.target_id = "b1";
    e1.trigger = SignalTrigger("GO_1");
    e1.transition_action = common_act;
    ir.add_transition(e1);

    TransitionEdge e2;
    e2.source_id = "hub";
    e2.target_id = "b2";
    e2.trigger = SignalTrigger("GO_2");
    e2.transition_action = common_act;
    ir.add_transition(e2);

    DiagnosticEngine diag;
    CommonActionFactoringPass pass;
    EXPECT_TRUE(pass.run(ir, diag));

    // Common action must be factored into hub exit actions
    const auto* h = ir.find_state("hub");
    ASSERT_NE(h, nullptr);
    ASSERT_EQ(h->exit_actions.size(), 1u);
    EXPECT_EQ(h->exit_actions[0].instructions.size(), 1u);

    for (const auto& t : ir.transitions) {
        EXPECT_FALSE(t.transition_action.has_value());
    }
}

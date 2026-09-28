/**
 * @file test_inlining_passes.cpp
 * @brief Unit tests for submachine and choice pseudo-state inlining canonicalization passes.
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/passes/choice_inlining_pass.hpp"
#include "fsm/middleend/passes/submachine_inlining_pass.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend::passes;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify SubmachineInliningPass graph splicing and entry port remapping.
 * @scenario Host FSM references submachine 'ProtocolFSM' inside composite state 'CommsHandler'.
 * @expected Submachine states and transitions are spliced with prefixed names into host FSM.
 */
TEST(SubmachineInlining, SubmachineReference_SplicedIntoHostCompositeState) {
    // 1. Build Submachine model
    FsmIr submachine_model;
    submachine_model.name = "ProtocolFSM";
    submachine_model.add_state("Connecting");
    submachine_model.add_state("Connected");
    submachine_model.add_transition("Connecting", "Connected", SignalTrigger{"HandshakeOk", ""});

    // 2. Build Host model
    FsmIr host_ir;
    host_ir.name = "HostSystem";
    host_ir.initial_state = "Standby";
    host_ir.add_state("Standby");

    auto& sub_state = host_ir.add_or_get_state("CommsHandler", "", StateKind::Composite);
    SubmachineRef ref("ProtocolFSM", "protocols/tcp.sysml");
    ref.port_mappings.emplace_back("Connecting", "Connected");
    sub_state.submachine = ref;

    SubmachineInliningPass pass([&](const std::string& name) -> const FsmIr* {
        if (name == "ProtocolFSM")
            return &submachine_model;
        return nullptr;
    });

    DiagnosticEngine diag;
    EXPECT_TRUE(pass.run(host_ir, diag));

    // Verify submachine was inlined
    const auto* inlined_comms = host_ir.find_state("CommsHandler");
    ASSERT_NE(inlined_comms, nullptr);
    EXPECT_FALSE(inlined_comms->submachine.has_value());
    EXPECT_TRUE(inlined_comms->is_composite);
    EXPECT_EQ(inlined_comms->initial_sub_state, "CommsHandler_Connecting");

    ASSERT_NE(host_ir.find_state("CommsHandler_Connecting"), nullptr);
    ASSERT_NE(host_ir.find_state("CommsHandler_Connected"), nullptr);
}

/**
 * @brief Verify ChoiceInliningPass flattens choice pseudostates into direct composite transitions.
 * @scenario FSM has state Idle, Choice node 'evaluate_health', and targets Nominal and Degraded.
 * @expected Choice pseudostate is removed and 2 direct transitions with flattened actions are synthesized.
 */
TEST(ChoiceInlining, DecisionBranches_FlattenedIntoCompositeTransitions) {
    FsmIr ir;
    ir.name = "ChoiceInliningFSM";
    ir.initial_state = "Idle";

    ir.add_state("Idle");
    ir.add_state("Nominal");
    ir.add_state("Degraded");

    ChoiceNodeModel choice;
    choice.name = "evaluate_health";
    ir.choice_nodes.push_back(choice);

    StateNode choice_st;
    choice_st.name = "evaluate_health";
    choice_st.kind = StateKind::Choice;
    ir.states.push_back(choice_st);

    // Incoming transition
    TransitionEdge in_t;
    in_t.source = "Idle";
    in_t.target = "evaluate_health";
    in_t.event = "StartCmd";
    in_t.set_action("InitSubsystem");
    ir.add_transition(in_t);

    // Branch 1: nominal
    TransitionEdge b1;
    b1.source = "evaluate_health";
    b1.target = "Nominal";
    b1.guard = "BatteryOk";
    b1.set_action("EnablePower");
    ir.add_transition(b1);

    // Branch 2: degraded (else)
    TransitionEdge b2;
    b2.source = "evaluate_health";
    b2.target = "Degraded";
    b2.guard = "else";
    b2.set_action("LogError");
    ir.add_transition(b2);

    ChoiceInliningPass pass;
    DiagnosticEngine diag;
    pass.run(ir, diag);

    // Verify choice state and choice node are eliminated
    EXPECT_EQ(ir.find_state("evaluate_health"), nullptr);
    EXPECT_TRUE(ir.choice_nodes.empty());
    EXPECT_EQ(ir.states.size(), 3u);

    // Verify exactly 2 inlined transitions
    ASSERT_EQ(ir.transitions.size(), 2u);

    // Check transition 1 (Idle -> Nominal)
    auto it_nom = std::find_if(ir.transitions.begin(), ir.transitions.end(),
                               [](const TransitionEdge& t) { return t.target == "Nominal"; });
    ASSERT_NE(it_nom, ir.transitions.end());
    EXPECT_EQ(it_nom->source, "Idle");
    EXPECT_EQ(it_nom->event, "StartCmd");
    ASSERT_TRUE(it_nom->guard.has_value());
    EXPECT_EQ(*it_nom->guard, "BatteryOk");
    EXPECT_EQ(it_nom->get_action(), "InitSubsystem_EnablePower");

    // Check transition 2 (Idle -> Degraded)
    auto it_deg = std::find_if(ir.transitions.begin(), ir.transitions.end(),
                               [](const TransitionEdge& t) { return t.target == "Degraded"; });
    ASSERT_NE(it_deg, ir.transitions.end());
    EXPECT_EQ(it_deg->source, "Idle");
    EXPECT_EQ(it_deg->event, "StartCmd");
    EXPECT_FALSE(it_deg->guard.has_value());  // 'else' guard is unwrapped
    EXPECT_EQ(it_deg->get_action(), "InitSubsystem_LogError");
}

}  // namespace

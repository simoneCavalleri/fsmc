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

    // Verify source_id and target_id synchronization
    ASSERT_FALSE(host_ir.transitions.empty());
    const auto* connecting_st = host_ir.find_state("CommsHandler_Connecting");
    const auto* connected_st = host_ir.find_state("CommsHandler_Connected");
    EXPECT_EQ(host_ir.transitions[0].source_id, connecting_st->id);
    EXPECT_EQ(host_ir.transitions[0].target_id, connected_st->id);
}

/**
 * @brief Verify SubmachineInliningPass clones Choice nodes and machine-scoped variables with proper prefixes.
 */
TEST(SubmachineInlining, ChoiceNodesAndVariables_InlinedWithSynchronizedIds) {
    FsmIr sub_ir;
    sub_ir.name = "SubModule";
    sub_ir.add_state("S1");
    sub_ir.add_state("S2");

    ChoiceNodeModel cn;
    cn.name = "DecideNext";
    sub_ir.choice_nodes.push_back(cn);

    VariableDefinition vdef;
    vdef.name = "internal_var";
    vdef.type = "uint32_t";
    vdef.initial_value = "10";
    sub_ir.variables.push_back(vdef);

    sub_ir.add_transition("S1", "S2", SignalTrigger{"Tick", ""});

    FsmIr host_ir;
    host_ir.name = "TopHost";
    host_ir.initial_state = "Idle";
    host_ir.add_state("Idle");

    auto& comp_st = host_ir.add_or_get_state("Worker", "", StateKind::Composite);
    SubmachineRef ref("SubModule", "sub.sysml");
    comp_st.submachine = ref;

    SubmachineInliningPass pass([&](const std::string& name) -> const FsmIr* {
        if (name == "SubModule")
            return &sub_ir;
        return nullptr;
    });

    DiagnosticEngine diag;
    EXPECT_TRUE(pass.run(host_ir, diag));

    // Choice node inlined and prefixed
    EXPECT_TRUE(host_ir.is_choice_node("Worker_DecideNext"));

    // Variable inlined and prefixed
    const auto* imported_var = host_ir.find_variable("Worker_internal_var");
    ASSERT_NE(imported_var, nullptr);
    EXPECT_EQ(imported_var->initial_value, "10");

    // Transition ID synchronization
    ASSERT_FALSE(host_ir.transitions.empty());
    const auto* s1_st = host_ir.find_state("Worker_S1");
    const auto* s2_st = host_ir.find_state("Worker_S2");
    ASSERT_NE(s1_st, nullptr);
    ASSERT_NE(s2_st, nullptr);
    EXPECT_EQ(host_ir.transitions[0].source_id, s1_st->id);
    EXPECT_EQ(host_ir.transitions[0].target_id, s2_st->id);
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

/**
 * @brief Verify that a transition through a choice node returning to the source state
 *        is preserved as an External self-transition (not downgraded to Internal).
 */
TEST(ChoiceInlining, SelfTransitionThroughChoice_PreservesExternalKind) {
    FsmIr ir;
    ir.name = "ChoiceSelfTransitionFSM";
    ir.initial_state = "Processing";

    ir.add_state("Processing");
    ir.add_choice_node("Decision");

    StateNode choice_st;
    choice_st.name = "Decision";
    choice_st.kind = StateKind::Choice;
    ir.states.push_back(choice_st);

    TransitionEdge in_t;
    in_t.source = "Processing";
    in_t.target = "Decision";
    in_t.event = "EvCheck";
    in_t.kind = TransitionEdgeKind::External;
    ir.add_transition(in_t);

    TransitionEdge out_t;
    out_t.source = "Decision";
    out_t.target = "Processing";
    out_t.guard = "RetryNeeded";
    out_t.kind = TransitionEdgeKind::External;
    ir.add_transition(out_t);

    ChoiceInliningPass pass;
    DiagnosticEngine diag;
    pass.run(ir, diag);

    ASSERT_EQ(ir.transitions.size(), 1u);
    const auto& inlined = ir.transitions[0];
    EXPECT_EQ(inlined.source, "Processing");
    EXPECT_EQ(inlined.target, "Processing");
    EXPECT_EQ(inlined.kind, TransitionEdgeKind::External);
}

TEST(ChoiceInlining, CombinedGuards_ProduceTargetAgnosticGuardAst) {
    FsmIr ir;
    ir.name = "ChoiceGuardAstFSM";
    ir.initial_state = "Idle";
    ir.add_state("Idle");
    ir.add_state("Choice1", "", StateKind::Choice);
    ir.add_state("Ready");

    TransitionEdge in_t("Idle", "Choice1", "StartCmd");
    in_t.set_guard("PowerOk");
    ir.add_transition(in_t);

    TransitionEdge out_t("Choice1", "Ready", "");
    out_t.set_guard("DoorClosed");
    ir.add_transition(out_t);

    ChoiceInliningPass pass;
    DiagnosticEngine diag;
    ASSERT_TRUE(pass.run(ir, diag));
    ASSERT_EQ(ir.transitions.size(), 1u);

    const auto& combined = ir.transitions[0];
    ASSERT_TRUE(combined.guard_ast.has_value());
    EXPECT_EQ(combined.guard_ast->op, GuardOp::And);
    EXPECT_EQ(combined.guard_ast->to_string(), "PowerOk && DoorClosed");
    EXPECT_EQ(combined.guard.value_or("").find("fsm::and_"), std::string::npos);
}

/**
 * @brief Regression: `else` fallback branches in ChoiceInliningPass must sort after guarded branches.
 * @scenario Inline a choice node with a guarded branch to `Working` and an `else` branch to `ErrorState`.
 * @expected The guarded branch receives higher priority (smaller non-zero priority) than the `else` fallback.
 */
TEST(InliningPasses, ChoiceInlining_ElseBranchHasLowerPriorityThanGuardedBranch) {
    FsmIr ir;
    ir.name = "ChoiceElsePrio";
    ir.initial_state = "Working";
    ir.add_state("Working");
    ir.add_state("ErrorState");
    ir.add_state("Decide", "", StateKind::Choice);
    ir.add_event("EvCheck");

    ir.add_transition(TransitionEdge("Working", "Decide", "EvCheck"));
    TransitionEdge t_ok("Decide", "Working", "");
    t_ok.set_guard("sensor_ok_eq_true");
    ir.add_transition(std::move(t_ok));

    TransitionEdge t_else("Decide", "ErrorState", "");
    t_else.set_guard("else");
    ir.add_transition(std::move(t_else));

    ChoiceInliningPass pass;
    DiagnosticEngine diag;
    ASSERT_TRUE(pass.run(ir, diag));
    ASSERT_EQ(ir.transitions.size(), 2u);

    const TransitionEdge* guarded_edge = nullptr;
    const TransitionEdge* else_edge = nullptr;
    for (const auto& t : ir.transitions) {
        if (t.target == "Working") {
            guarded_edge = &t;
        } else if (t.target == "ErrorState") {
            else_edge = &t;
        }
    }
    ASSERT_NE(guarded_edge, nullptr);
    ASSERT_NE(else_edge, nullptr);
    EXPECT_GT(guarded_edge->priority, 0u);
    EXPECT_EQ(else_edge->priority, 0u);
}

/**
 * @brief Regression: `ChoiceInliningPass` prunes unreachable choice pseudostates (`incoming.empty()`)
 * and their dangling outgoing transitions so C++ backend validation does not reject with `ECPP004`.
 * @scenario Run `ChoiceInliningPass` on a model containing an unreachable choice node `C_dead -> Dead`.
 * @expected `C_dead` is removed from `choice_nodes` and `states`, and its dangling transition is pruned.
 */
TEST(InliningPasses, ChoiceInlining_PrunesUnreachableChoicePseudostates) {
    FsmIr ir;
    ir.name = "UnreachableChoice";
    ir.initial_state = "Init";
    ir.add_state("Init");
    ir.add_state("Dead");
    ir.add_choice_node("C_dead");
    ir.add_transition(TransitionEdge("C_dead", "Dead", ""));

    ChoiceInliningPass pass;
    DiagnosticEngine diag;
    ASSERT_TRUE(pass.run(ir, diag));
    EXPECT_FALSE(ir.is_choice_node("C_dead"));
    EXPECT_TRUE(ir.choice_nodes.empty());
    EXPECT_TRUE(ir.transitions.empty());
}

/**
 * @brief Verify SubmachineInliningPass in modular mode (null resolver), unresolved model, and recursive self-inlining.
 */
TEST(SubmachineInlining, ModularUnresolvedAndRecursive_DiagnosticsHandledGracefully) {
    // 1. No resolver -> modular info diagnostic, submachine ref preserved
    {
        FsmIr ir;
        ir.name = "HostModular";
        auto& st = ir.add_or_get_state("SubHost", "", StateKind::Composite);
        st.submachine = SubmachineRef("ExternalFsm", "ext.sysml");

        SubmachineInliningPass modular_pass;
        DiagnosticEngine diag;
        EXPECT_TRUE(modular_pass.run(ir, diag));
        EXPECT_FALSE(diag.has_errors());
        EXPECT_TRUE(ir.find_state("SubHost")->submachine.has_value());
    }

    // 2. Resolver returns nullptr -> W_SUBMACHINE_NOT_FOUND
    {
        FsmIr ir;
        ir.name = "HostMissing";
        auto& st = ir.add_or_get_state("MissingHost", "", StateKind::Composite);
        st.submachine = SubmachineRef("UnknownFsm", "missing.sysml");

        SubmachineInliningPass pass([](const std::string&) -> const FsmIr* { return nullptr; });
        DiagnosticEngine diag;
        EXPECT_TRUE(pass.run(ir, diag));
        EXPECT_FALSE(diag.has_errors());
        EXPECT_TRUE(ir.find_state("MissingHost")->submachine.has_value());
    }

    // 3. Recursive self-inlining -> E_SUBMACHINE_RECURSIVE
    {
        FsmIr ir;
        ir.name = "SelfMachine";
        auto& st = ir.add_or_get_state("Loop", "", StateKind::Composite);
        st.submachine = SubmachineRef("SelfMachine", "self.sysml");

        SubmachineInliningPass pass([&](const std::string&) -> const FsmIr* { return &ir; });
        DiagnosticEngine diag;
        EXPECT_TRUE(pass.run(ir, diag));
        EXPECT_TRUE(diag.has_errors());
    }
}

/**
 * @brief Verify SubmachineInliningPass rewrites variables across ASTs, guards, actions, and nested hierarchy.
 */
TEST(SubmachineInlining, VariableRewritingAndNestedHierarchy_RewritesAllEntitiesCorrectly) {
    FsmIr sub;
    sub.name = "RichSub";
    sub.initial_state = "Outer";

    VariableDefinition v_cnt;
    v_cnt.name = "cnt";
    v_cnt.type = "int32_t";
    v_cnt.initial_value = "0";
    sub.variables.push_back(v_cnt);

    PortDefinition out_port;
    out_port.name = "out_p";
    out_port.type = DataType::int32();
    out_port.direction = PortDirection::Out;
    sub.ports.push_back(out_port);
    sub.signals.emplace_back("DoneSig");
    sub.clocks.emplace_back("sub_clk");
    TypeDefinition sub_cfg;
    sub_cfg.name = "SubCfg";
    sub.add_type(sub_cfg);
    sub.add_guard("g_cnt", "cnt check", std::string("cnt > 0 && obj.cnt == 0 && \"cnt\""), std::string("cnt > 0"));

    {
        auto& outer = sub.add_or_get_state("Outer", "", StateKind::Composite);
        outer.initial_sub_state = "Inner";
    }
    sub.add_or_get_state("Inner", "Outer");
    sub.add_or_get_state("ExitNode", "");

    ExpressionAstNode raw_ast;
    raw_ast.kind = ExpressionKind::RawExpression;
    raw_ast.symbol = "cnt + 1";
    ExpressionAstNode var_ast;
    var_ast.kind = ExpressionKind::VariableRef;
    var_ast.symbol = "cnt";

    // Entry/exit actions with StoreOp, PortWriteOp, PortReadOp, SignalEmitOp, ActionCallOp, ExpressionAstNode
    ActionSignature entry_act("sub_entry");
    entry_act.invocation = "log(cnt)";
    entry_act.assignments.emplace_back(LValueTarget("cnt"), "cnt + 1", AssignmentOp::Assign);
    entry_act.assignments.back().expr_ast = raw_ast;
    StoreOp st_op;
    st_op.target = LValueTarget("cnt");
    st_op.expression = "cnt + 2";
    st_op.expr_ast = var_ast;
    entry_act.instructions.emplace_back(st_op);
    PortWriteOp pw_op;
    pw_op.port_name = "out_p";
    pw_op.expression = "cnt";
    pw_op.expr_ast = var_ast;
    entry_act.instructions.emplace_back(pw_op);
    PortReadOp pr_op;
    pr_op.destination = LValueTarget("cnt");
    entry_act.instructions.emplace_back(pr_op);
    SignalEmitOp sig_op;
    sig_op.signal_name = "DoneSig";
    sig_op.arguments.push_back("cnt");
    entry_act.instructions.emplace_back(sig_op);
    ActionCallOp call_op;
    call_op.function_name = "helper";
    call_op.arguments.push_back("cnt");
    entry_act.instructions.emplace_back(call_op);
    auto* outer_ptr = sub.find_state_mut("Outer");
    auto* exit_ptr = sub.find_state_mut("ExitNode");
    ASSERT_NE(outer_ptr, nullptr);
    ASSERT_NE(exit_ptr, nullptr);
    outer_ptr->entry_actions.push_back(entry_act);
    exit_ptr->exit_actions.push_back(entry_act);

    GuardAstNode inv;
    inv.expression = "cnt >= 0";
    outer_ptr->invariants.push_back(inv);

    TransitionEdge sub_t("Inner", "ExitNode", "DoneSig");
    sub_t.set_guard("cnt > 0");
    sub_t.transition_action = entry_act;
    sub_t.condition_action = entry_act;
    sub.add_transition(sub_t);

    FsmIr host;
    host.name = "RichHost";
    host.initial_state = "Idle";
    host.add_state("Idle");
    host.add_state("Finished");

    auto& call_st = host.add_or_get_state("Sub", "", StateKind::Composite);
    SubmachineRef ref("RichSub", "rich.sysml");
    ref.port_mappings.emplace_back("Outer", "ExitNode");
    call_st.submachine = ref;
    host.add_transition("Idle", "Outer", SignalTrigger{"Start", ""});
    host.add_transition("ExitNode", "Finished", SignalTrigger{"Complete", ""});

    SubmachineInliningPass pass([&](const std::string& n) -> const FsmIr* { return n == "RichSub" ? &sub : nullptr; });
    DiagnosticEngine diag;
    ASSERT_TRUE(pass.run(host, diag));
    EXPECT_FALSE(diag.has_errors());

    EXPECT_NE(host.find_variable("Sub_cnt"), nullptr);
    EXPECT_NE(host.find_port("out_p"), nullptr);
    EXPECT_NE(host.find_signal("DoneSig"), nullptr);
    EXPECT_FALSE(host.clocks.empty());
    EXPECT_TRUE(host.has_type("SubCfg"));

    const auto* inlined_outer = host.find_state("Sub_Outer");
    ASSERT_NE(inlined_outer, nullptr);
    EXPECT_EQ(inlined_outer->initial_sub_state, "Sub_Inner");
    ASSERT_FALSE(inlined_outer->entry_actions.empty());
    EXPECT_EQ(inlined_outer->entry_actions[0].invocation, "log(Sub_cnt)");
    EXPECT_EQ(inlined_outer->entry_actions[0].assignments[0].target.name, "Sub_cnt");
    EXPECT_EQ(inlined_outer->entry_actions[0].assignments[0].expression, "Sub_cnt + 1");
    ASSERT_FALSE(inlined_outer->invariants.empty());
    EXPECT_EQ(inlined_outer->invariants[0].expression, "Sub_cnt >= 0");

    // Verify port remapping on host transitions
    bool found_entry_remap = false;
    bool found_exit_remap = false;
    for (const auto& t : host.transitions) {
        if (t.source == "Idle" && t.target == "Sub_Outer") {
            found_entry_remap = true;
        }
        if (t.source == "Sub_ExitNode" && t.target == "Finished") {
            found_exit_remap = true;
        }
    }
    EXPECT_TRUE(found_entry_remap);
    EXPECT_TRUE(found_exit_remap);
}

/**
 * @brief Regression (BUG-47): SubmachineInliningPass replaces existing placeholder entryPoint/exitPoint
 * states in `host.states` instead of appending duplicates.
 */
TEST(SubmachineInlining, PlaceholderEntryExitStates_ReplacedWithoutDuplicates) {
    FsmIr sub;
    sub.name = "AuthSub";
    sub.initial_state = "Check";
    sub.add_state("ep_fast", "", StateKind::EntryPoint);
    sub.add_state("Check");
    sub.add_state("xp_ok", "", StateKind::ExitPoint);
    sub.add_transition(TransitionEdge("ep_fast", "Check", ""));
    sub.add_transition(TransitionEdge("Check", "xp_ok", "EvPass"));

    FsmIr host;
    host.name = "AppHost";
    host.initial_state = "Idle";
    host.add_state("Idle");
    host.add_state("Done");
    auto& login = host.add_or_get_state("Login", "", StateKind::Composite);
    login.submachine = SubmachineRef("AuthSub");
    // Host pre-declares placeholder states matching prefixed entry/exit names
    host.add_state("Login_ep_fast");
    host.add_state("Login_xp_ok");
    host.add_transition(TransitionEdge("Idle", "Login_ep_fast", "EvStart"));
    host.add_transition(TransitionEdge("Login_xp_ok", "Done", ""));

    SubmachineInliningPass pass([&](const std::string& n) -> const FsmIr* { return n == "AuthSub" ? &sub : nullptr; });
    DiagnosticEngine diag;
    ASSERT_TRUE(pass.run(host, diag));
    EXPECT_FALSE(diag.has_errors());

    int ep_count = 0;
    int xp_count = 0;
    for (const auto& st : host.states) {
        if (st.name == "Login_ep_fast") {
            ++ep_count;
            EXPECT_EQ(st.kind, StateKind::EntryPoint);
        }
        if (st.name == "Login_xp_ok") {
            ++xp_count;
            EXPECT_EQ(st.kind, StateKind::ExitPoint);
        }
    }
    EXPECT_EQ(ep_count, 1);
    EXPECT_EQ(xp_count, 1);
}

/**
 * @brief Regression (BUG-49): ChoiceInliningPass conjoins negated sibling guards on an `[else]` branch
 * targeting a downstream choice node so chained choices remain mutually exclusive.
 */
TEST(ChoiceInlining, ChainedChoiceElseBranch_NegatesSiblingGuard) {
    FsmIr ir;
    ir.name = "ChainedChoice";
    ir.initial_state = "Idle";
    ir.add_state("Idle");
    ir.add_state("C1", "", StateKind::Choice);
    ir.add_state("C2", "", StateKind::Choice);
    ir.add_state("High");
    ir.add_state("Med");
    ir.add_state("Low");

    ir.add_transition(TransitionEdge("Idle", "C1", "EvCheck"));
    ir.add_transition(TransitionEdge("C1", "High", "", "v > 100"));
    ir.add_transition(TransitionEdge("C1", "C2", "", "else"));
    ir.add_transition(TransitionEdge("C2", "Med", "", "v > 50"));
    ir.add_transition(TransitionEdge("C2", "Low", "", "else"));

    ChoiceInliningPass pass;
    DiagnosticEngine diag;
    ASSERT_TRUE(pass.run(ir, diag));

    EXPECT_EQ(ir.find_state("C1"), nullptr);
    EXPECT_EQ(ir.find_state("C2"), nullptr);
    ASSERT_EQ(ir.transitions.size(), 3u);

    const TransitionEdge* to_med = nullptr;
    const TransitionEdge* to_low = nullptr;
    for (const auto& tr : ir.transitions) {
        if (tr.target == "Med")
            to_med = &tr;
        if (tr.target == "Low")
            to_low = &tr;
    }
    ASSERT_NE(to_med, nullptr);
    ASSERT_NE(to_low, nullptr);
    ASSERT_TRUE(to_med->guard.has_value());
    EXPECT_NE(to_med->guard->find("!(v > 100)"), std::string::npos);
    EXPECT_NE(to_med->guard->find("v > 50"), std::string::npos);
    ASSERT_TRUE(to_low->guard.has_value());
    EXPECT_NE(to_low->guard->find("!(v > 100)"), std::string::npos);
}

}  // namespace

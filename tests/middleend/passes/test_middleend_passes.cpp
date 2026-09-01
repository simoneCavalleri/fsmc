#include <gtest/gtest.h>

#include "fsm/middleend/passes/dead_state_pruning_pass.hpp"
#include "fsm/middleend/passes/determinism_enforcement_pass.hpp"
#include "fsm/middleend/passes/guard_simplification_pass.hpp"
#include "fsm/middleend/passes/orthogonal_interference_pass.hpp"
#include "fsm/middleend/pass_manager.hpp"
#include "fsm/middleend/passes/submachine_inlining_pass.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify algebraic boolean simplifications (double negation, constant folding, idempotency).
 *
 * Scenario:
 * - Simplify !(!Ready) -> Ready.
 * - Simplify Ready && true -> Ready.
 * - Simplify Ready && false -> false.
 * - Simplify Ready || false -> Ready.
 * - Simplify Ready || true -> true.
 * - Simplify Ready && Ready -> Ready.
 */
TEST(MiddleendPassesTest, GuardSimplificationAlgebraicReductions) {
    // 1. Double negation: !(!Ready) -> Ready
    GuardAstNode double_not(GuardOp::Not, {GuardAstNode(GuardOp::Not, {GuardAstNode("Ready")})});
    GuardAstNode res_dn = GuardSimplificationPass::simplify_node(double_not);
    EXPECT_EQ(res_dn.op, GuardOp::None);
    EXPECT_EQ(res_dn.expression, "Ready");

    // 2. AND with true: (Ready && true) -> Ready
    GuardAstNode and_true(GuardOp::And, {GuardAstNode("Ready"), GuardAstNode("true")});
    GuardAstNode res_at = GuardSimplificationPass::simplify_node(and_true);
    EXPECT_EQ(res_at.op, GuardOp::None);
    EXPECT_EQ(res_at.expression, "Ready");

    // 3. AND with false: (Ready && false) -> false
    GuardAstNode and_false(GuardOp::And, {GuardAstNode("Ready"), GuardAstNode("false")});
    GuardAstNode res_af = GuardSimplificationPass::simplify_node(and_false);
    EXPECT_EQ(res_af.op, GuardOp::None);
    EXPECT_EQ(res_af.expression, "false");

    // 4. OR with false: (Ready || false) -> Ready
    GuardAstNode or_false(GuardOp::Or, {GuardAstNode("Ready"), GuardAstNode("false")});
    GuardAstNode res_of = GuardSimplificationPass::simplify_node(or_false);
    EXPECT_EQ(res_of.op, GuardOp::None);
    EXPECT_EQ(res_of.expression, "Ready");

    // 5. OR with true: (Ready || true) -> true
    GuardAstNode or_true(GuardOp::Or, {GuardAstNode("Ready"), GuardAstNode("true")});
    GuardAstNode res_ot = GuardSimplificationPass::simplify_node(or_true);
    EXPECT_EQ(res_ot.op, GuardOp::None);
    EXPECT_EQ(res_ot.expression, "true");

    // 6. Idempotency: (Ready && Ready) -> Ready
    GuardAstNode dup_and(GuardOp::And, {GuardAstNode("Ready"), GuardAstNode("Ready")});
    GuardAstNode res_da = GuardSimplificationPass::simplify_node(dup_and);
    EXPECT_EQ(res_da.op, GuardOp::None);
    EXPECT_EQ(res_da.expression, "Ready");
}

/**
 * @brief Test Intent: Verify OrthogonalInterferencePass detects concurrent data races in parallel (AND) states.
 *
 * Scenario:
 * - Construct parallel state with RegA and RegB concurrently modifying `battery_level`.
 * - Verify pass emits a SafetyCritical diagnostic (W_CONCURRENT_DATA_RACE).
 */
TEST(MiddleendPassesTest, OrthogonalInterferenceDataRaceDetection) {
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
    t_a.action_sig = act_a;
    ir.add_transition(t_a);

    // Transition in RegB ALSO mutating battery_level -> Data Race!
    TransitionEdge t_b;
    t_b.source = "StateB1";
    t_b.target = "StateB2";
    t_b.event = "TickB";
    ActionSignature act_b("ActB", "ActB");
    act_b.assignments.emplace_back("battery_level", "battery_level + 10");
    t_b.action_sig = act_b;
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
 * @brief Test Intent: Verify DeterminismEnforcementPass canonical priority sorting and collision detection.
 *
 * Scenario:
 * - Define multiple transitions from Idle for StartCmd with priorities 2 and 1.
 * - Verify pass sorts priority 1 before priority 2 in the transition table.
 */
TEST(MiddleendPassesTest, DeterminismEnforcementAndPriorityOrdering) {
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
 * @brief Test Intent: Verify SubmachineInliningPass graph splicing and entry port remapping.
 *
 * Scenario:
 * - Host FSM references submachine `ProtocolFSM`.
 * - Provide submachine model to SubmachineInliningPass resolver.
 * - Verify submachine states and transitions are seamlessly spliced into the parent graph.
 */
TEST(MiddleendPassesTest, SubmachineInliningSplicing) {
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
 * @brief Test Intent: Verify DeadStatePruningPass removes unreachable states and dead transitions.
 *
 * Scenario:
 * - FSM has reachable path Init -> Active.
 * - FSM has unreachable Island state and a transition with guard == "false".
 * - Verify pass prunes Island and the false transition from the IR.
 */
TEST(MiddleendPassesTest, DeadStateAndTransitionPruning) {
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
 * @brief Test Intent: Verify ChoiceInliningPass flattens choice pseudostates into direct composite transitions.
 *
 * Scenario:
 * - FSM has state Idle, Choice node evaluate_health, targets Nominal and Degraded.
 * - Idle -> evaluate_health (event StartCmd, action InitSubsystem).
 * - evaluate_health -> Nominal (guard BatteryOk, action EnablePower).
 * - evaluate_health -> Degraded (guard else, action LogError).
 * - Verify pass flattens into 2 direct transitions (Idle -> Nominal, Idle -> Degraded) with combined actions.
 */
TEST(MiddleendPassesTest, ChoiceInliningBranchFlattening) {
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
    in_t.action = "InitSubsystem";
    ir.add_transition(in_t);

    // Branch 1: nominal
    TransitionEdge b1;
    b1.source = "evaluate_health";
    b1.target = "Nominal";
    b1.guard = "BatteryOk";
    b1.action = "EnablePower";
    ir.add_transition(b1);

    // Branch 2: degraded (else)
    TransitionEdge b2;
    b2.source = "evaluate_health";
    b2.target = "Degraded";
    b2.guard = "else";
    b2.action = "LogError";
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
    ASSERT_TRUE(it_nom->action.has_value());
    EXPECT_EQ(*it_nom->action, "InitSubsystem_EnablePower");

    // Check transition 2 (Idle -> Degraded)
    auto it_deg = std::find_if(ir.transitions.begin(), ir.transitions.end(),
                               [](const TransitionEdge& t) { return t.target == "Degraded"; });
    ASSERT_NE(it_deg, ir.transitions.end());
    EXPECT_EQ(it_deg->source, "Idle");
    EXPECT_EQ(it_deg->event, "StartCmd");
    EXPECT_FALSE(it_deg->guard.has_value());  // 'else' guard is unwrapped
    ASSERT_TRUE(it_deg->action.has_value());
    EXPECT_EQ(*it_deg->action, "InitSubsystem_LogError");
}

/**
 * @brief Test Intent: Verify determinism enforcement detects non-deterministic collisions on identical-priority branches.
 */
TEST(MiddleendPassesTest, DeterminismEnforcementUnconditionalCollision) {
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

/**
 * @brief Test Intent: Verify EFSM interval analysis validates port domain bounds and detects contract violations.
 *
 * Scenario:
 * - Define InPort `sensor_val` in [0, 100] and OutPort `actuator_cmd` in [0, 200].
 * - Run interval analysis.
 * - Verify no errors on compliant models.
 */
TEST(MiddleendPassesTest, EfsmIntervalAnalysisContractVerification) {
    FsmIr model;
    model.name = "BoundedFSM";

    PortDefinition in_p("sensor_val", "float", PortDirection::In);
    in_p.min_value = 0.0;
    in_p.max_value = 100.0;
    model.ports.push_back(in_p);

    PortDefinition out_p("actuator_cmd", "float", PortDirection::Out);
    out_p.min_value = 0.0;
    out_p.max_value = 200.0;
    model.ports.push_back(out_p);

    DiagnosticEngine diag;
    EFSMIntervalAnalyzer interval_pass(model);
    auto findings = interval_pass.analyze(diag);
    EXPECT_FALSE(diag.has_errors());
}

/**
 * @brief Test Intent: Verify EFSM interval analyzer detects out-of-range assignments violating OutPort contracts.
 *
 * Scenario:
 * - Define OutPort `heater_power` with range [0.0, 100.0].
 * - Add transition with action assigning `heater_power = 150.0f`.
 * - Verify analyzer emits W_PORT_RANGE_VIOLATION diagnostic.
 */
TEST(MiddleendPassesTest, EfsmIntervalAnalysisOutOfRangePortAssignment) {
    FsmIr model;
    model.name = "ThermostatFSM";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Heating");

    PortDefinition out_p("heater_power", "float", PortDirection::Out);
    out_p.min_value = 0.0;
    out_p.max_value = 100.0;
    model.ports.push_back(out_p);

    TransitionEdge t("t1", "Idle", "Heating", SignalTrigger("EvStart"));
    ActionSignature act("OverheatAct", "OverheatAct");
    act.assignments.push_back({"heater_power", "150.0f"});
    t.action_sig = act;
    model.add_transition(t);

    DiagnosticEngine diag;
    EFSMIntervalAnalyzer analyzer(model);
    auto findings = analyzer.analyze(diag);

    bool found_port_violation = false;
    for (const auto& f : findings) {
        if (f.variable_name == "heater_power" && f.is_error) {
            found_port_violation = true;
            break;
        }
    }
    EXPECT_TRUE(found_port_violation);
}

/**
 * @brief Test Intent: Verify EFSM interval analyzer detects unsatisfiable guards over bounded InPorts.
 *
 * Scenario:
 * - Define InPort `sensor_temp` bounded to [-50.0, 50.0].
 * - Transition has guard `in.sensor_temp > 90.0f`.
 * - Verify analyzer detects unsatisfiable guard and reports diagnostic.
 */
TEST(MiddleendPassesTest, EfsmIntervalAnalysisUnsatisfiableGuardDetection) {
    FsmIr model;
    model.name = "SensorFSM";
    model.initial_state = "Active";
    model.add_state("Active");
    model.add_state("Alert");

    PortDefinition in_p("sensor_temp", "float", PortDirection::In);
    in_p.min_value = -50.0;
    in_p.max_value = 50.0;
    model.ports.push_back(in_p);

    GuardModel gm("OverheatGuard", "OverheatGuard", "in.sensor_temp > 90.0f", "in.sensor_temp > 90.0f");
    model.guards.push_back(gm);

    TransitionEdge t("t1", "Active", "Alert", AnonymousTrigger{});
    t.guard = "OverheatGuard";
    model.add_transition(t);

    DiagnosticEngine diag;
    EFSMIntervalAnalyzer analyzer(model);
    auto findings = analyzer.analyze(diag);

    bool found_unsat_guard = false;
    for (const auto& f : findings) {
        if (f.variable_name == "sensor_temp" && !f.is_error) {
            found_unsat_guard = true;
            break;
        }
    }
    EXPECT_TRUE(found_unsat_guard);
}

}  // namespace

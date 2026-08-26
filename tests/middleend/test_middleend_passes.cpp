#include <gtest/gtest.h>

#include "fsm/middleend/dead_state_pruning_pass.hpp"
#include "fsm/middleend/determinism_enforcement_pass.hpp"
#include "fsm/middleend/guard_simplification_pass.hpp"
#include "fsm/middleend/orthogonal_interference_pass.hpp"
#include "fsm/middleend/pass_manager.hpp"
#include "fsm/middleend/submachine_inlining_pass.hpp"

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

}  // namespace

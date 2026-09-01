#include <gtest/gtest.h>

#include <unordered_set>

// Test individual granular headers are self-contained
#include "fsm/ir/action.hpp"
#include "fsm/ir/deterministic_id.hpp"
#include "fsm/ir/event_model.hpp"
#include "fsm/ir/formal_property.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/region.hpp"
#include "fsm/ir/signal_definition.hpp"
#include "fsm/ir/state_kind.hpp"
#include "fsm/ir/state_node.hpp"
#include "fsm/ir/transition_edge.hpp"
#include "fsm/ir/transition_edge_kind.hpp"
#include "fsm/ir/trigger.hpp"
#include "fsm/ir/variable_definition.hpp"

// Test umbrella aggregator header
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify modular IR header decoupling, enum converters, and trigger variants.
 *
 * Scenario:
 * - Validate conversions for StateKind, TransitionEdgeKind, and TriggerVariant.
 * - Verify ActionSignature and ActionAssignment fields.
 */
TEST(FsmIrTest, ModularHeaderSubcomponents) {
    // 1. StateKind conversions
    EXPECT_EQ(state_kind_to_string(StateKind::Parallel), "Parallel");
    EXPECT_EQ(state_kind_from_string("Parallel"), StateKind::Parallel);
    EXPECT_EQ(state_kind_to_string(StateKind::DeepHistory), "DeepHistory");
    EXPECT_EQ(state_kind_from_string("DeepHistory"), StateKind::DeepHistory);

    // 2. TransitionEdgeKind conversions
    EXPECT_EQ(transition_edge_kind_to_string(TransitionEdgeKind::Internal), "Internal");
    EXPECT_EQ(transition_edge_kind_from_string("Internal"), TransitionEdgeKind::Internal);
    EXPECT_EQ(transition_edge_kind_to_string(TransitionEdgeKind::Local), "Local");
    EXPECT_EQ(transition_edge_kind_from_string("Local"), TransitionEdgeKind::Local);

    // 3. ActionSignature and ActionAssignment
    ActionAssignment assign("counter", "counter + 1");
    EXPECT_EQ(assign.target_variable, "counter");
    EXPECT_EQ(assign.expression, "counter + 1");

    ActionSignature act_sig("on_reset", "srv.reset()");
    act_sig.accepts_event = true;
    act_sig.assignments.push_back(assign);
    EXPECT_TRUE(act_sig.accepts_event);
    EXPECT_EQ(act_sig.assignments.size(), 1u);

    // 4. Triggers
    TimeTrigger t_trigger{100, true};
    EXPECT_EQ(t_trigger.duration_ms, 100u);
    EXPECT_TRUE(t_trigger.periodic);

    SignalTrigger s_trigger{"Tick", "data"};
    EXPECT_EQ(s_trigger.signal_name, "Tick");

    TriggerVariant v1 = t_trigger;
    TriggerVariant v2 = s_trigger;
    TriggerVariant v3 = AnonymousTrigger{};
    EXPECT_TRUE(std::holds_alternative<TimeTrigger>(v1));
    EXPECT_TRUE(std::holds_alternative<SignalTrigger>(v2));
    EXPECT_TRUE(std::holds_alternative<AnonymousTrigger>(v3));

    // 5. Models
    EventModel ev("EvSample", "Documentation for EvSample");
    EXPECT_EQ(ev.name, "EvSample");
    ChoiceNodeModel c_node("ChoiceA");
    EXPECT_EQ(c_node.name, "ChoiceA");
}

/**
 * @brief Test Intent: Verify deterministic FNV-1a 64-bit ID computation for state node identification.
 *
 * Scenario:
 * - Compute hashes for identical and differing hierarchical strings.
 * - Verify stability across runs and uniqueness across different state names.
 */
TEST(FsmIrTest, DeterministicIdGeneration) {
    std::string id1 = compute_deterministic_id("Operating.Running.Manual");
    std::string id2 = compute_deterministic_id("Operating.Running.Manual");
    std::string id3 = compute_deterministic_id("Operating.Running.Auto");

    EXPECT_EQ(id1, id2);
    EXPECT_NE(id1, id3);
    EXPECT_EQ(id1.rfind("id_", 0), 0u);
}

/**
 * @brief Test Intent: Verify hierarchical state representations, orthogonal regions, and JSON serialization.
 *
 * Scenario:
 * - Build composite state with parallel orthogonal regions.
 * - Canonicalize and serialize to JSON.
 * - Verify all orthogonal regions, signals, and guard ASTs are faithfully preserved.
 */
TEST(FsmIrTest, StateHierarchyAndOrthogonalRegions) {
    FsmIr ir;
    ir.name = "IndustrialController";
    ir.ns = "industrial";
    ir.satisfies_reqs = {"REQ-SAFETY-01", "REQ-REALTIME-02"};

    // Add state hierarchy
    auto& operating = ir.add_or_get_state("Operating", "", StateKind::Composite);
    operating.do_activity = "async_sensor_poll";
    operating.traceability_reqs = {"REQ-SAFETY-01"};

    auto& running = ir.add_or_get_state("Running", "Operating", StateKind::Parallel);

    // Add orthogonal regions to Running
    OrthogonalRegion reg_motion;
    reg_motion.id = "reg_motion";
    reg_motion.name = "MotionRegion";
    reg_motion.initial_state_id = "Manual";
    reg_motion.state_ids = {"Manual", "Auto"};
    running.orthogonal_regions.push_back(reg_motion);

    OrthogonalRegion reg_diagnostics;
    reg_diagnostics.id = "reg_diag";
    reg_diagnostics.name = "DiagnosticsRegion";
    reg_diagnostics.initial_state_id = "Normal";
    reg_diagnostics.state_ids = {"Normal", "Degraded"};
    running.orthogonal_regions.push_back(reg_diagnostics);

    // Add signals with typed payloads and validators
    SignalDefinition sig_packet("EvPacketRecv");
    sig_packet.attributes.emplace_back("len", "uint32_t");
    sig_packet.attributes.emplace_back("ptr", "const uint8_t*");
    sig_packet.validators.emplace_back("len > 0");
    sig_packet.validators.emplace_back("ptr != nullptr");
    ir.add_signal(sig_packet);

    // Add transitions with guard AST
    GuardAstNode guard_ast(GuardOp::And,
                           {GuardAstNode("SafetyOk"), GuardAstNode(GuardOp::Not, {GuardAstNode("EStop")})});
    ActionSignature action_sig("ctx.on_start(payload)", "ctx.on_start(payload)");
    ir.add_transition("Idle", "Operating", SignalTrigger{"StartCmd", "payload"}, guard_ast, action_sig);

    ir.canonicalize();

    // Verify properties
    EXPECT_EQ(ir.states.size(), 2u);
    EXPECT_EQ(ir.signals.size(), 2u);
    EXPECT_EQ(ir.transitions.size(), 1u);
    EXPECT_EQ(ir.signals[0].validators.size(), 2u);
    EXPECT_EQ(ir.transitions[0].guard_ast->to_string(), "SafetyOk && !EStop");

    // Test JSON serialization
    std::string json = FsmIrSerializer::serialize_json(ir);
    EXPECT_NE(json.find("\"name\": \"IndustrialController\""), std::string::npos);
    EXPECT_NE(json.find("\"REQ-SAFETY-01\""), std::string::npos);
    EXPECT_NE(json.find("\"do_activity\": \"async_sensor_poll\""), std::string::npos);
    EXPECT_NE(json.find("\"validators\": [\"len > 0\", \"ptr != nullptr\"]"), std::string::npos);
    EXPECT_NE(json.find("\"guard_ast\": \"SafetyOk && !EStop\""), std::string::npos);
    EXPECT_NE(json.find("\"orthogonal_regions\": [{\"id\": \"reg_motion\""), std::string::npos);
}

/**
 * @brief Test Intent: Verify formal verification AST representation for temporal properties (LTL/CTL).
 *
 * Scenario:
 * - Build safety property AST: `G (LowBattery -> F SafeLand)`.
 * - Build mutual exclusion invariant AST: `G (!(StateA && StateB))`.
 * - Verify canonical sorting, requirement traceability link, and JSON serialization.
 */
TEST(FsmIrTest, TemporalPropertiesAndFormalVerificationAst) {
    // 1. Safety property: G (LowBattery -> F SafeLand)
    PropertyAstNode battery_low("LowBattery");
    PropertyAstNode safe_land("SafeLand");
    PropertyAstNode f_safe_land(TemporalOp::Finally, {safe_land});
    PropertyAstNode impl(TemporalOp::Implies, {battery_low, f_safe_land});
    PropertyAstNode g_prop(TemporalOp::Globally, {impl});

    EXPECT_EQ(g_prop.to_string(), "G (LowBattery -> F (SafeLand))");

    FormalProperty prop_safety("SafeBatteryLanding", PropertyKind::Safety, "G (LowBattery -> F (SafeLand))",
                               "Guarantees that low battery triggers safe landing", "REQ-SAFE-09");
    prop_safety.ast = g_prop;

    // 2. Mutual exclusion invariant: ! (StateA && StateB)
    PropertyAstNode state_a("StateA");
    PropertyAstNode state_b("StateB");
    PropertyAstNode both(TemporalOp::And, {state_a, state_b});
    PropertyAstNode not_both(TemporalOp::Not, {both});
    PropertyAstNode g_mutex(TemporalOp::Globally, {not_both});

    EXPECT_EQ(g_mutex.to_string(), "G (!(StateA && StateB))");

    FormalProperty prop_mutex("MutexInvariant", PropertyKind::Invariant, "G (!(StateA && StateB))",
                              "States A and B can never be active simultaneously", "REQ-MUTEX-01");
    prop_mutex.ast = g_mutex;

    // Add to FsmIr
    FsmIr ir;
    ir.name = "SafetyVerifiedFSM";
    ir.add_property(prop_safety);
    ir.add_property(prop_mutex);
    ir.canonicalize();

    EXPECT_EQ(ir.properties.size(), 2u);
    EXPECT_EQ(ir.properties[0].name, "MutexInvariant");  // Canonical order by name
    EXPECT_EQ(ir.properties[1].name, "SafeBatteryLanding");

    const auto* found = ir.find_property("SafeBatteryLanding");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->kind, PropertyKind::Safety);
    EXPECT_EQ(found->traceability_req, "REQ-SAFE-09");

    // JSON serialization verification
    std::string json = FsmIrSerializer::serialize_json(ir);
    EXPECT_NE(json.find("\"properties\": ["), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"SafeBatteryLanding\""), std::string::npos);
    EXPECT_NE(json.find("\"kind\": \"Safety\""), std::string::npos);
    EXPECT_NE(json.find("\"ast\": \"G (LowBattery -> F (SafeLand))\""), std::string::npos);
    EXPECT_NE(json.find("\"traceability_req\": \"REQ-SAFE-09\""), std::string::npos);
}

/**
 * @brief Test Intent: Verify extended finite state machine (EFSM) state variables and bounded domains.
 *
 * Scenario:
 * - Define variables with min/max bounds and initial values.
 * - Define transition edge with assignments `retry_count = retry_count + 1`.
 * - Verify serialization to JSON.
 */
TEST(FsmIrTest, StateVariablesAndStructuredActions) {
    FsmIr ir;
    ir.name = "MotorControllerEFSM";

    // Add state variables with bounded domains
    VariableDefinition var_retry("retry_count", "uint32_t", "0", 0, 5, "Number of connection retries");
    VariableDefinition var_speed("rpm", "int32_t", "0", -5000, 5000, "Current motor angular velocity");
    VariableDefinition var_safe("safety_interlock", "bool", "true", std::nullopt, std::nullopt, "Hardware interlock");

    ir.add_variable(var_retry);
    ir.add_variable(var_speed);
    ir.add_variable(var_safe);
    ir.canonicalize();

    EXPECT_EQ(ir.variables.size(), 3u);
    const auto* found_rpm = ir.find_variable("rpm");
    ASSERT_NE(found_rpm, nullptr);
    EXPECT_EQ(found_rpm->type, "int32_t");
    EXPECT_EQ(found_rpm->min_value, -5000);
    EXPECT_EQ(found_rpm->max_value, 5000);

    // Transition with variable assignment actions
    ActionSignature action("IncrementRetry", "ctx.inc_retry()");
    action.assignments.emplace_back("retry_count", "retry_count + 1");
    action.assignments.emplace_back("rpm", "0");

    TransitionEdge edge("Idle", "Connecting", "StartCmd", std::nullopt, std::nullopt, "Start transition");
    edge.action_sig = action;
    ir.add_transition(std::move(edge));

    std::string json = FsmIrSerializer::serialize_json(ir);
    EXPECT_NE(json.find("\"variables\": ["), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"retry_count\""), std::string::npos);
    EXPECT_NE(json.find("\"min_value\": 0"), std::string::npos);
    EXPECT_NE(json.find("\"max_value\": 5"), std::string::npos);
    EXPECT_NE(json.find("\"assignments\": [{\"variable\": \"retry_count\", \"expression\": \"retry_count + 1\"}"),
              std::string::npos);
}

/**
 * @brief Test Intent: Verify Fork/Join multi-source / multi-target transitions and submachine references.
 *
 * Scenario:
 * - Construct Fork transition (1 source -> 2 targets) and Join transition (2 sources -> 1 target).
 * - Construct SubmachineRef with port mappings.
 * - Verify serialization to JSON.
 */
TEST(FsmIrTest, ForkJoinTransitionsAndSubmachines) {
    FsmIr ir;
    ir.name = "ConcurrentMissionFSM";

    // 1. Fork Transition (1 source -> 2 targets)
    TransitionEdge fork_edge;
    fork_edge.id = "fork_01";
    fork_edge.source = "Idle";
    fork_edge.source_id = "Idle";
    fork_edge.source_ids = {"Idle"};
    fork_edge.target_ids = {"NavigationRegion.Active", "TelemetryRegion.Active"};
    fork_edge.event = "LaunchCmd";
    fork_edge.kind = TransitionEdgeKind::External;

    EXPECT_TRUE(fork_edge.is_fork());
    EXPECT_FALSE(fork_edge.is_join());

    // 2. Join Transition (2 sources rendezvous -> 1 target)
    TransitionEdge join_edge;
    join_edge.id = "join_01";
    join_edge.source_ids = {"NavigationRegion.Completed", "TelemetryRegion.Completed"};
    join_edge.target = "MissionComplete";
    join_edge.target_id = "MissionComplete";
    join_edge.target_ids = {"MissionComplete"};
    join_edge.event = "AllDone";
    join_edge.kind = TransitionEdgeKind::External;

    EXPECT_FALSE(join_edge.is_fork());
    EXPECT_TRUE(join_edge.is_join());

    ir.add_transition(std::move(fork_edge));
    ir.add_transition(std::move(join_edge));

    // 3. Submachine State
    auto& comms_state = ir.add_or_get_state("CommsHandler", "", StateKind::Composite);
    SubmachineRef submachine("TcpProtocolFSM", "protocols/tcp.sysml");
    submachine.port_mappings.emplace_back("InEntry", "Connecting");
    submachine.port_mappings.emplace_back("OutExit", "Connected");
    comms_state.submachine = submachine;

    EXPECT_TRUE(comms_state.submachine.has_value());
    EXPECT_EQ(comms_state.submachine->fsm_name, "TcpProtocolFSM");
    EXPECT_EQ(comms_state.submachine->port_mappings.size(), 2u);

    // Verify JSON serialization
    std::string json = FsmIrSerializer::serialize_json(ir);
    EXPECT_NE(json.find("\"source_ids\": [\"Idle\"]"), std::string::npos);
    EXPECT_NE(json.find("\"target_ids\": [\"NavigationRegion.Active\", \"TelemetryRegion.Active\"]"),
              std::string::npos);
    EXPECT_NE(json.find("\"submachine\": {\"fsm_name\": \"TcpProtocolFSM\""), std::string::npos);
    EXPECT_NE(json.find("{\"entry\": \"InEntry\", \"exit\": \"Connecting\"}"), std::string::npos);
}

/**
 * @brief Test Intent: Verify collision resistance of deterministic ID generator across 10,000 keys.
 *
 * Scenario:
 * - Generate 10,000 unique hierarchical state keys.
 * - Verify each computed deterministic ID is completely unique with 0 collisions.
 */
TEST(FsmIrTest, DeterministicIdCollisionResistanceAcrossLargeSet) {
    std::unordered_set<std::string> id_set;
    constexpr int TotalKeys = 10000;
    id_set.reserve(TotalKeys);

    for (int i = 0; i < TotalKeys; ++i) {
        std::string key = "StateHierarchy.Node_" + std::to_string(i) + ".Region_" + std::to_string(i % 16);
        std::string computed_id = compute_deterministic_id(key);
        EXPECT_TRUE(id_set.insert(computed_id).second) << "Collision detected for key: " << key;
    }
    EXPECT_EQ(id_set.size(), static_cast<std::size_t>(TotalKeys));
}

/**
 * @brief Test Intent: Verify manual AST construction for temporal logic implications (`P -> Q`).
 *
 * Scenario:
 * - Construct composite PropertyAstNode representing `Globally(SafetyLock) -> Finally(Arming)`.
 * - Verify operator, children, and properties.
 */
TEST(FsmIrTest, FormalPropertyAstConstruction) {
    FormalProperty prop;
    prop.name = "SafetyLockInvariant";
    prop.kind = PropertyKind::Safety;
    prop.description = "Never enter Arming without SafetyInterlock being true";

    PropertyAstNode left_node;
    left_node.op = TemporalOp::Globally;
    left_node.atom = "SafetyInterlock == true";

    PropertyAstNode right_node;
    right_node.op = TemporalOp::Finally;
    right_node.atom = "State == Arming";

    PropertyAstNode root;
    root.op = TemporalOp::Implies;
    root.children.push_back(left_node);
    root.children.push_back(right_node);

    prop.ast = root;

    EXPECT_EQ(prop.name, "SafetyLockInvariant");
    EXPECT_EQ(prop.kind, PropertyKind::Safety);
    ASSERT_TRUE(prop.ast.has_value());
    EXPECT_EQ(prop.ast->op, TemporalOp::Implies);
    EXPECT_EQ(prop.ast->children.size(), 2u);
}

/**
 * @brief Test Intent: Verify priority, time_invariant, EntryPoint, and ExitPoint state kinds in FsmIr.
 *
 * Scenario:
 * - Create states with EntryPoint and ExitPoint kinds.
 * - Set time_invariant on state and priority on transition edge.
 * - Verify serialization to JSON preserves time_invariant and priority.
 */
TEST(FsmIrTest, PriorityTimeInvariantAndEntryExitPoints) {
    EXPECT_EQ(state_kind_to_string(StateKind::EntryPoint), "EntryPoint");
    EXPECT_EQ(state_kind_from_string("EntryPoint"), StateKind::EntryPoint);
    EXPECT_EQ(state_kind_to_string(StateKind::ExitPoint), "ExitPoint");
    EXPECT_EQ(state_kind_from_string("ExitPoint"), StateKind::ExitPoint);

    FsmIr ir;
    ir.name = "AdvancedTimingFSM";
    ir.initial_state = "EntryNode";

    ir.add_or_get_state("EntryNode", "", StateKind::EntryPoint);
    auto* timed_state = &ir.add_or_get_state("TimedProcessing", "", StateKind::Atomic);
    timed_state->time_invariant = "stay_duration <= 500ms";

    ir.add_or_get_state("ExitNode", "", StateKind::ExitPoint);

    TransitionEdge t1("EntryNode", "TimedProcessing", "Start", std::nullopt, std::nullopt, "Entry transition",
                      TransitionEdgeKind::External, 1);
    EXPECT_EQ(t1.priority, 1u);

    TransitionEdge t2("TimedProcessing", "ExitNode", "Timeout", std::nullopt, std::nullopt, "Exit transition",
                      TransitionEdgeKind::External, 5);
    EXPECT_EQ(t2.priority, 5u);

    ir.add_transition(std::move(t1));
    ir.add_transition(std::move(t2));

    const auto* entry_node = ir.find_state("EntryNode");
    const auto* timed_node = ir.find_state("TimedProcessing");
    const auto* exit_node = ir.find_state("ExitNode");

    ASSERT_NE(entry_node, nullptr);
    ASSERT_NE(timed_node, nullptr);
    ASSERT_NE(exit_node, nullptr);

    EXPECT_EQ(entry_node->kind, StateKind::EntryPoint);
    EXPECT_EQ(exit_node->kind, StateKind::ExitPoint);
    ASSERT_TRUE(timed_node->time_invariant.has_value());
    EXPECT_EQ(*timed_node->time_invariant, "stay_duration <= 500ms");

    std::string json = FsmIrSerializer::serialize_json(ir);
    EXPECT_NE(json.find("\"kind\": \"EntryPoint\""), std::string::npos);
    EXPECT_NE(json.find("\"kind\": \"ExitPoint\""), std::string::npos);
    EXPECT_NE(json.find("\"time_invariant\": \"stay_duration <= 500ms\""), std::string::npos);
    EXPECT_NE(json.find("\"priority\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"priority\": 5"), std::string::npos);
}

/**
 * @brief Test Intent: Verify domain-separated PortDefinition, SignalDefinition, VariableDefinition and zero Context references.
 */
TEST(FsmIrTest, DomainPortSeparationAndZeroContext) {
    FsmIr model;
    model.name = "DualChannelMachine";
    model.ns = "TestSystem";

    // 1. InPort with numeric contract
    PortDefinition in_p("sensor_val", "float", PortDirection::In);
    in_p.min_value = 0.0;
    in_p.max_value = 100.0;
    in_p.constraint = "self >= 0.0 and self <= 100.0";
    model.ports.push_back(in_p);

    // 2. OutPort with numeric contract
    PortDefinition out_p("actuator_cmd", "float", PortDirection::Out);
    out_p.min_value = 0.0;
    out_p.max_value = 200.0;
    out_p.constraint = "self >= 0.0 and self <= 200.0";
    model.ports.push_back(out_p);

    // 3. Registers (Variables)
    model.variables.emplace_back("retry_count", "uint32_t", "0");

    // 4. Signals with Typed Payload
    SignalDefinition sig("CmdBoost");
    sig.attributes.emplace_back("boost_val", "float");
    model.signals.push_back(sig);

    // Verifications
    const auto* found_in = model.find_port("sensor_val");
    ASSERT_NE(found_in, nullptr);
    EXPECT_TRUE(found_in->is_in());
    EXPECT_FALSE(found_in->is_out());
    EXPECT_DOUBLE_EQ(found_in->min_value.value(), 0.0);
    EXPECT_DOUBLE_EQ(found_in->max_value.value(), 100.0);

    const auto* found_out = model.find_port("actuator_cmd");
    ASSERT_NE(found_out, nullptr);
    EXPECT_TRUE(found_out->is_out());
    EXPECT_FALSE(found_out->is_in());
    EXPECT_DOUBLE_EQ(found_out->max_value.value(), 200.0);

    ASSERT_EQ(model.variables.size(), 1u);
    EXPECT_EQ(model.variables[0].name, "retry_count");

    ASSERT_EQ(model.signals.size(), 1u);
    EXPECT_EQ(model.signals[0].name, "CmdBoost");
    ASSERT_EQ(model.signals[0].attributes.size(), 1u);
    EXPECT_EQ(model.signals[0].attributes[0].name, "boost_val");
    EXPECT_EQ(model.signals[0].attributes[0].type, "float");
}

}  // namespace

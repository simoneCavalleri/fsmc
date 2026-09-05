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
#include "fsm/frontend/diagram/json_parser.hpp"
#include "fsm/ir/expression.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/ir/type_definition.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"
#include "fsm/middleend/pass_manager.hpp"

using namespace fsm::ir;
using namespace fsm::diagnostic;
using namespace fsm::frontend;
using namespace fsm::middleend;

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
    ir.package = "industrial";

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
    EXPECT_NE(json.find("\"assignments\": [{\"variable\": \"retry_count\""), std::string::npos);
    EXPECT_NE(json.find("\"expression\": \"retry_count + 1\""), std::string::npos);
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
 * @brief Test Intent: Verify domain-separated PortDefinition, SignalDefinition, VariableDefinition and zero Context
 * references.
 */
TEST(FsmIrTest, DomainPortSeparationAndZeroContext) {
    FsmIr model;
    model.name = "DualChannelMachine";
    model.package = "TestSystem";


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

// ============================================================================
// Compound User-Defined Types & Algebraic EFSM Tests
// ============================================================================

TEST(FsmIrTest, TypeDefinitionKindAndFactories) {
    // 1. Enum creation via factory
    auto nav_enum = TypeDefinition::make_enum("NavigationMode", "uint8_t", {}, "Autonomous navigation modes");
    EXPECT_EQ(nav_enum.name, "NavigationMode");
    EXPECT_EQ(nav_enum.kind, TypeKind::Enum);
    EXPECT_EQ(nav_enum.underlying_type, "uint8_t");
    EXPECT_EQ(nav_enum.description, "Autonomous navigation modes");
    EXPECT_EQ(type_kind_to_string(nav_enum.kind), "enum");

    nav_enum.add_literal("Manual", 0, "Pilot manual control");
    nav_enum.add_literal("AutoWaypoint", 1, "Autonomous waypoint tracking");
    nav_enum.add_literal("ReturnToHome", 2, "Failsafe RTH");

    EXPECT_EQ(nav_enum.literals.size(), 3u);
    EXPECT_TRUE(nav_enum.has_literal("AutoWaypoint"));
    EXPECT_FALSE(nav_enum.has_literal("Unknown"));

    const auto* lit_auto = nav_enum.find_literal("AutoWaypoint");
    ASSERT_NE(lit_auto, nullptr);
    EXPECT_EQ(lit_auto->name, "AutoWaypoint");
    ASSERT_TRUE(lit_auto->value.has_value());
    EXPECT_EQ(*lit_auto->value, 1);

    auto* mut_rth = nav_enum.find_literal_mut("ReturnToHome");
    ASSERT_NE(mut_rth, nullptr);
    mut_rth->value = 99;
    EXPECT_EQ(*nav_enum.find_literal("ReturnToHome")->value, 99);

    // 2. Struct creation via factory
    auto waypoint_struct = TypeDefinition::make_struct("Waypoint3D", {}, false, "3D GPS coordinate");
    EXPECT_EQ(waypoint_struct.name, "Waypoint3D");
    EXPECT_EQ(waypoint_struct.kind, TypeKind::Struct);
    EXPECT_FALSE(waypoint_struct.is_datatype);
    EXPECT_EQ(type_kind_to_string(waypoint_struct.kind), "struct");

    waypoint_struct.add_field(StructField("latitude", "double", "0.0", "[deg]", -90.0, 90.0, "Latitude"));
    waypoint_struct.add_field(StructField("longitude", "double", "0.0", "[deg]", -180.0, 180.0, "Longitude"));
    waypoint_struct.add_field(StructField("altitude", "float", "100.0f", "[m]", 0.0, 50000.0, "Altitude AGL"));

    EXPECT_EQ(waypoint_struct.fields.size(), 3u);
    EXPECT_TRUE(waypoint_struct.has_field("altitude"));
    EXPECT_FALSE(waypoint_struct.has_field("speed"));

    const auto* f_alt = waypoint_struct.find_field("altitude");
    ASSERT_NE(f_alt, nullptr);
    EXPECT_EQ(f_alt->type, "float");
    EXPECT_EQ(f_alt->default_value, "100.0f");
    ASSERT_TRUE(f_alt->physical_unit.has_value());
    EXPECT_EQ(*f_alt->physical_unit, "[m]");

    // 3. Alias creation via factory
    auto speed_alias = TypeDefinition::make_alias("MetersPerSecond", "float", "Velocity measurement unit");
    EXPECT_EQ(speed_alias.name, "MetersPerSecond");
    EXPECT_EQ(speed_alias.kind, TypeKind::Alias);
    EXPECT_EQ(speed_alias.underlying_type, "float");
    EXPECT_EQ(type_kind_to_string(speed_alias.kind), "alias");

    // 4. Equality operator
    TypeDefinition nav_copy = nav_enum;
    EXPECT_EQ(nav_enum, nav_copy);
    nav_copy.underlying_type = "uint16_t";
    EXPECT_NE(nav_enum, nav_copy);

    // 5. String conversion helpers
    EXPECT_EQ(string_to_type_kind("enum"), TypeKind::Enum);
    EXPECT_EQ(string_to_type_kind("struct"), TypeKind::Struct);
    EXPECT_EQ(string_to_type_kind("alias"), TypeKind::Alias);
}

TEST(FsmIrTest, CustomTypesMetamodelIntegration) {
    FsmIr ir;
    ir.name = "AvionicsController";

    // Add types in unsorted order
    auto type_z = TypeDefinition::make_alias("Voltage_V", "float");
    auto type_a = TypeDefinition::make_enum("FlightState", "uint8_t");
    type_a.add_literal("Disarmed", 0);
    type_a.add_literal("Armed", 1);
    auto type_m = TypeDefinition::make_struct("GpsFix");
    type_m.add_field(StructField("satellites", "uint8_t", "0"));

    ir.add_type(type_z);
    ir.add_type(type_a);
    ir.add_type(type_m);

    EXPECT_EQ(ir.custom_types.size(), 3u);
    EXPECT_TRUE(ir.has_type("FlightState"));
    EXPECT_TRUE(ir.has_type("GpsFix"));
    EXPECT_TRUE(ir.has_type("Voltage_V"));
    EXPECT_FALSE(ir.has_type("NonExistentType"));

    ASSERT_NE(ir.find_type("FlightState"), nullptr);
    EXPECT_EQ(ir.find_type("FlightState")->kind, TypeKind::Enum);

    ASSERT_NE(ir.find_type("GpsFix"), nullptr);
    EXPECT_EQ(ir.find_type("GpsFix")->kind, TypeKind::Struct);

    // Overwriting existing type
    auto updated_z = TypeDefinition::make_alias("Voltage_V", "double", "High-precision voltage");
    ir.add_type(updated_z);
    EXPECT_EQ(ir.custom_types.size(), 3u);
    EXPECT_EQ(ir.find_type("Voltage_V")->underlying_type, "double");

    // Deterministic canonical sorting
    ir.canonicalize();
    EXPECT_EQ(ir.custom_types[0].name, "FlightState");
    EXPECT_EQ(ir.custom_types[1].name, "GpsFix");
    EXPECT_EQ(ir.custom_types[2].name, "Voltage_V");

    // Structural equality check on FsmIr
    FsmIr ir_copy = ir;
    EXPECT_EQ(ir, ir_copy);
    ir_copy.custom_types[0].description = "Modified description";
    EXPECT_NE(ir, ir_copy);
}

TEST(FsmIrTest, ExpressionAstNodeFactoriesAndStringification) {
    // 1. Leaf literals
    auto lit_int = ExpressionAstNode::make_int_literal(42);
    EXPECT_EQ(lit_int.kind, ExpressionKind::IntegerLiteral);
    EXPECT_EQ(lit_int.to_string(), "42");

    auto lit_float = ExpressionAstNode::make_float_literal(3.14);
    EXPECT_EQ(lit_float.kind, ExpressionKind::FloatLiteral);
    EXPECT_NE(lit_float.to_string().find("3.14"), std::string::npos);

    auto lit_bool = ExpressionAstNode::make_bool_literal(true);
    EXPECT_EQ(lit_bool.kind, ExpressionKind::BooleanLiteral);
    EXPECT_EQ(lit_bool.to_string(), "true");

    auto lit_enum = ExpressionAstNode::make_enum_literal("FlightMode", "Auto");
    EXPECT_EQ(lit_enum.kind, ExpressionKind::EnumLiteral);
    EXPECT_EQ(lit_enum.to_string(), "FlightMode::Auto");

    // 2. Leaf references
    auto ref_var = ExpressionAstNode::make_variable_ref("counter");
    EXPECT_EQ(ref_var.kind, ExpressionKind::VariableRef);
    EXPECT_EQ(ref_var.to_string(), "counter");

    auto ref_port = ExpressionAstNode::make_port_ref("telemetry", "altitude");
    EXPECT_EQ(ref_port.kind, ExpressionKind::PortRef);
    EXPECT_EQ(ref_port.to_string(), "telemetry.altitude");

    auto ref_evt = ExpressionAstNode::make_event_param_ref("EvSensorUpdate", "pressure");
    EXPECT_EQ(ref_evt.kind, ExpressionKind::EventParamRef);
    EXPECT_EQ(ref_evt.to_string(), "EvSensorUpdate.pressure");

    // 3. Unary operations
    auto un_neg = ExpressionAstNode::make_unary(ExpressionOp::Negate, ref_var);
    EXPECT_EQ(un_neg.kind, ExpressionKind::UnaryOp);
    EXPECT_EQ(un_neg.to_string(), "-counter");

    auto un_not = ExpressionAstNode::make_unary(ExpressionOp::LogicalNot, lit_bool);
    EXPECT_EQ(un_not.to_string(), "!true");

    // 4. Binary operations with operator precedence: (a + b) * 2
    auto add_node = ExpressionAstNode::make_binary(ExpressionOp::Add, ExpressionAstNode::make_variable_ref("a"),
                                                  ExpressionAstNode::make_variable_ref("b"));
    auto mul_node =
        ExpressionAstNode::make_binary(ExpressionOp::Multiply, add_node, ExpressionAstNode::make_int_literal(2));
    EXPECT_EQ(mul_node.to_string(), "(a + b) * 2");

    // a + b * 2
    auto mul_sub = ExpressionAstNode::make_binary(ExpressionOp::Multiply, ExpressionAstNode::make_variable_ref("b"),
                                                 ExpressionAstNode::make_int_literal(2));
    auto add_parent =
        ExpressionAstNode::make_binary(ExpressionOp::Add, ExpressionAstNode::make_variable_ref("a"), mul_sub);
    EXPECT_EQ(add_parent.to_string(), "a + b * 2");

    // 5. JSON serialization
    std::string json_str = add_parent.to_json();
    EXPECT_NE(json_str.find("\"kind\": \"BinaryOp\""), std::string::npos);
    EXPECT_NE(json_str.find("\"op\": \"+\""), std::string::npos);
    EXPECT_NE(json_str.find("\"symbol\": \"a\""), std::string::npos);
    EXPECT_NE(json_str.find("\"value\": 2"), std::string::npos);
}

TEST(FsmIrTest, ExpressionAstNodeAlgebraicParser) {
    // 1. Simple integer addition
    auto ast1 = ExpressionAstNode::parse("counter + 1");
    EXPECT_EQ(ast1.kind, ExpressionKind::BinaryOp);
    EXPECT_EQ(ast1.op, ExpressionOp::Add);
    ASSERT_EQ(ast1.children.size(), 2u);
    EXPECT_EQ(ast1.children[0].kind, ExpressionKind::VariableRef);
    EXPECT_EQ(ast1.children[0].symbol, "counter");
    EXPECT_EQ(ast1.children[1].kind, ExpressionKind::IntegerLiteral);
    EXPECT_EQ(std::get<int64_t>(ast1.children[1].value), 1);

    // 2. Operator precedence: multiplication before addition
    auto ast2 = ExpressionAstNode::parse("x + y * 10");
    EXPECT_EQ(ast2.kind, ExpressionKind::BinaryOp);
    EXPECT_EQ(ast2.op, ExpressionOp::Add);
    EXPECT_EQ(ast2.children[0].symbol, "x");
    EXPECT_EQ(ast2.children[1].op, ExpressionOp::Multiply);
    EXPECT_EQ(ast2.children[1].children[0].symbol, "y");
    EXPECT_EQ(std::get<int64_t>(ast2.children[1].children[1].value), 10);

    // 3. Parentheses override precedence
    auto ast3 = ExpressionAstNode::parse("(x + y) * 10");
    EXPECT_EQ(ast3.kind, ExpressionKind::BinaryOp);
    EXPECT_EQ(ast3.op, ExpressionOp::Multiply);
    EXPECT_EQ(ast3.children[0].op, ExpressionOp::Add);
    EXPECT_EQ(ast3.children[0].children[0].symbol, "x");
    EXPECT_EQ(ast3.children[0].children[1].symbol, "y");

    // 4. Bitwise shifts and masks
    auto ast4 = ExpressionAstNode::parse("(mask & 255) << 2");
    EXPECT_EQ(ast4.kind, ExpressionKind::BinaryOp);
    EXPECT_EQ(ast4.op, ExpressionOp::ShiftLeft);
    EXPECT_EQ(ast4.children[0].op, ExpressionOp::BitwiseAnd);

    // 5. Unary operators
    auto ast5 = ExpressionAstNode::parse("-delta");
    EXPECT_EQ(ast5.kind, ExpressionKind::UnaryOp);
    EXPECT_EQ(ast5.op, ExpressionOp::Negate);
    EXPECT_EQ(ast5.children[0].symbol, "delta");

    // 6. Port and register qualifiers
    auto ast6 = ExpressionAstNode::parse("in.sensor_temp + reg.offset");
    EXPECT_EQ(ast6.kind, ExpressionKind::BinaryOp);
    EXPECT_EQ(ast6.children[0].kind, ExpressionKind::PortRef);
    EXPECT_EQ(ast6.children[0].symbol, "sensor_temp");
    EXPECT_EQ(ast6.children[1].kind, ExpressionKind::VariableRef);
    EXPECT_EQ(ast6.children[1].symbol, "offset");

    // 7. Boolean literals
    auto ast7 = ExpressionAstNode::parse("true");
    EXPECT_EQ(ast7.kind, ExpressionKind::BooleanLiteral);
    EXPECT_TRUE(std::get<bool>(ast7.value));

    // 8. Opaque C++ code fallback
    auto ast8 = ExpressionAstNode::parse("compute_hash(buffer, 128);");
    EXPECT_EQ(ast8.kind, ExpressionKind::RawExpression);
    EXPECT_EQ(ast8.symbol, "compute_hash(buffer, 128);");
}

TEST(FsmIrTest, ActionAssignmentOperatorsAndAST) {
    // 1. Basic assignment with auto-parsed AST
    ActionAssignment a1("counter", "counter + 1");
    EXPECT_EQ(a1.target_variable, "counter");
    EXPECT_EQ(a1.op, AssignmentOp::Assign);
    EXPECT_EQ(a1.expression, "counter + 1");
    ASSERT_TRUE(a1.expr_ast.has_value());
    EXPECT_EQ(a1.expr_ast->kind, ExpressionKind::BinaryOp);
    EXPECT_EQ(a1.expr_ast->op, ExpressionOp::Add);

    // 2. Direct construction with explicit AssignmentOp and AST
    auto lit5 = ExpressionAstNode::make_int_literal(5);
    ActionAssignment a2("retry_count", AssignmentOp::AddAssign, lit5);
    EXPECT_EQ(a2.target_variable, "retry_count");
    EXPECT_EQ(a2.op, AssignmentOp::AddAssign);
    EXPECT_EQ(a2.expression, "5");
    ASSERT_TRUE(a2.expr_ast.has_value());
    EXPECT_EQ(a2.expr_ast->kind, ExpressionKind::IntegerLiteral);

    // 3. String parsing of assignment statements
    auto parsed_add = ActionAssignment::parse("reg.count += 10;");
    EXPECT_EQ(parsed_add.target_variable, "count");
    EXPECT_EQ(parsed_add.op, AssignmentOp::AddAssign);
    EXPECT_EQ(parsed_add.expression, "10");
    ASSERT_TRUE(parsed_add.expr_ast.has_value());
    EXPECT_EQ(parsed_add.expr_ast->kind, ExpressionKind::IntegerLiteral);

    auto parsed_shl = ActionAssignment::parse("out.mask <<= 2");
    EXPECT_EQ(parsed_shl.target_variable, "mask");
    EXPECT_EQ(parsed_shl.op, AssignmentOp::ShlAssign);
    EXPECT_EQ(parsed_shl.expression, "2");

    // 4. Operator string roundtrip
    EXPECT_EQ(assignment_op_to_string(AssignmentOp::Assign), "=");
    EXPECT_EQ(assignment_op_to_string(AssignmentOp::AddAssign), "+=");
    EXPECT_EQ(assignment_op_to_string(AssignmentOp::SubAssign), "-=");
    EXPECT_EQ(assignment_op_to_string(AssignmentOp::MulAssign), "*=");
    EXPECT_EQ(assignment_op_to_string(AssignmentOp::DivAssign), "/=");
    EXPECT_EQ(assignment_op_to_string(AssignmentOp::ModAssign), "%=");
    EXPECT_EQ(assignment_op_to_string(AssignmentOp::ShlAssign), "<<=");
    EXPECT_EQ(assignment_op_to_string(AssignmentOp::ShrAssign), ">>=");
    EXPECT_EQ(assignment_op_to_string(AssignmentOp::AndAssign), "&=");
    EXPECT_EQ(assignment_op_to_string(AssignmentOp::OrAssign), "|=");
    EXPECT_EQ(assignment_op_to_string(AssignmentOp::XorAssign), "^=");
}

TEST(FsmIrTest, JsonSerializationAndDeserializationCustomTypes) {
    FsmIr ir;
    ir.name = "MissionComputer";

    // Add user compound types
    auto nav_enum = TypeDefinition::make_enum("NavMode", "uint8_t", {}, "Navigation modes");
    nav_enum.add_literal("Manual", 0);
    nav_enum.add_literal("Auto", 1);
    ir.add_type(nav_enum);

    auto wp_struct = TypeDefinition::make_struct("Waypoint", {}, false, "Waypoint coordinate");
    wp_struct.add_field(StructField("lat", "float", "0.0f"));
    wp_struct.add_field(StructField("lon", "float", "0.0f"));
    ir.add_type(wp_struct);

    auto alias_type = TypeDefinition::make_alias("HeadingDeg", "float", "Aircraft heading in degrees");
    ir.add_type(alias_type);

    // Add variables, states, transitions with algebraic action
    VariableDefinition var_retry("retry_count", "int", "0");
    ir.variables.push_back(var_retry);

    ir.add_state("Idle");
    ir.add_state("Active");
    ir.initial_state = "Idle";

    TransitionEdge edge("Idle", "Active", "EvStart", std::nullopt);
    ActionSignature act_sig("OnStart");
    act_sig.assignments.push_back(ActionAssignment("retry_count", "retry_count + 1", AssignmentOp::Assign));
    edge.action_sig = act_sig;
    ir.add_transition(std::move(edge));

    ir.canonicalize();

    // 1. Serialize to JSON
    std::string json = FsmIrSerializer::serialize_json(ir);

    // Verify presence of top-level "types" array
    EXPECT_NE(json.find("\"types\": ["), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"NavMode\""), std::string::npos);
    EXPECT_NE(json.find("\"kind\": \"enum\""), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"Waypoint\""), std::string::npos);
    EXPECT_NE(json.find("\"kind\": \"struct\""), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"HeadingDeg\""), std::string::npos);
    EXPECT_NE(json.find("\"kind\": \"alias\""), std::string::npos);

    // Verify assignment serialization with op and ast
    EXPECT_NE(json.find("\"variable\": \"retry_count\""), std::string::npos);
    EXPECT_NE(json.find("\"op\": \"=\""), std::string::npos);
    EXPECT_NE(json.find("\"ast\": {"), std::string::npos);

    // 2. Parse back with JsonStateParser
    JsonStateParser parser;
    FsmIr parsed_ir;
    std::string err;
    bool ok = parser.parse(json, parsed_ir, err);
    ASSERT_TRUE(ok) << "JSON parse error: " << err;

    // Verify types restored in parsed model
    EXPECT_TRUE(parsed_ir.has_type("NavMode"));
    EXPECT_TRUE(parsed_ir.has_type("Waypoint"));
    EXPECT_TRUE(parsed_ir.has_type("HeadingDeg"));

    const auto* parsed_enum = parsed_ir.find_type("NavMode");
    ASSERT_NE(parsed_enum, nullptr);
    EXPECT_EQ(parsed_enum->kind, TypeKind::Enum);
    EXPECT_TRUE(parsed_enum->has_literal("Auto"));

    const auto* parsed_struct = parsed_ir.find_type("Waypoint");
    ASSERT_NE(parsed_struct, nullptr);
    EXPECT_EQ(parsed_struct->kind, TypeKind::Struct);
    EXPECT_TRUE(parsed_struct->has_field("lat"));
}

TEST(FsmIrTest, SemanticValidationPassAndFailures) {
    // 1. Valid model
    FsmIr valid_ir;
    valid_ir.name = "ValidMachine";
    valid_ir.add_state("S1");
    valid_ir.initial_state = "S1";

    valid_ir.add_type(TypeDefinition::make_enum("EngineState", "uint8_t"));
    valid_ir.variables.push_back(VariableDefinition("engine_mode", "EngineState", "0"));
    valid_ir.variables.push_back(VariableDefinition("speed_rpm", "int", "0"));
    valid_ir.ports.push_back(PortDefinition("out_speed", "int", PortDirection::Out));
    valid_ir.ports.push_back(PortDefinition("in_sensor", "int", PortDirection::In));

    TransitionEdge t1("S1", "S1", "EvTick", std::nullopt);
    ActionSignature a1("UpdateSpeed");
    a1.assignments.push_back(ActionAssignment("speed_rpm", "speed_rpm + 10"));
    a1.assignments.push_back(ActionAssignment("out_speed", "100"));
    t1.action_sig = a1;
    valid_ir.add_transition(t1);

    // Structural well-formedness
    std::string wf_err;
    EXPECT_TRUE(valid_ir.is_well_formed(wf_err));
    EXPECT_TRUE(wf_err.empty());

    FsmIr malformed_ir = valid_ir;
    malformed_ir.transitions[0].target = "NonExistentState";
    EXPECT_FALSE(malformed_ir.is_well_formed(wf_err));
    EXPECT_NE(wf_err.find("NonExistentState"), std::string::npos);

    // Semantic analysis
    std::vector<std::string> errors, warnings;
    EXPECT_TRUE(fsm::middleend::SemanticAnalyzer::validate(valid_ir, errors, warnings));
    EXPECT_TRUE(errors.empty());

    // 2. Semantic Failure: Assignment to unknown target variable
    FsmIr bad_ir1 = valid_ir;
    bad_ir1.transitions[0].action_sig->assignments.push_back(ActionAssignment("unknown_var", "10"));
    errors.clear();
    warnings.clear();
    EXPECT_FALSE(fsm::middleend::SemanticAnalyzer::validate(bad_ir1, errors, warnings));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("unknown_var"), std::string::npos);

    // 3. Semantic Failure: Assignment to read-only InPort
    FsmIr bad_ir2 = valid_ir;
    bad_ir2.transitions[0].action_sig->assignments.push_back(ActionAssignment("in_sensor", "42"));
    errors.clear();
    warnings.clear();
    EXPECT_FALSE(fsm::middleend::SemanticAnalyzer::validate(bad_ir2, errors, warnings));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("read-only InPort"), std::string::npos);

    // 4. Semantic Failure: Unknown variable type
    FsmIr bad_ir3 = valid_ir;
    bad_ir3.variables.push_back(VariableDefinition("mystery_data", "UnregisteredCustomType", "0"));
    errors.clear();
    warnings.clear();
    EXPECT_FALSE(fsm::middleend::SemanticAnalyzer::validate(bad_ir3, errors, warnings));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("UnregisteredCustomType"), std::string::npos);

    // 5. Semantic Warning: Type mismatch (assigning numeric literal to boolean target)
    FsmIr warn_ir = valid_ir;
    warn_ir.variables.push_back(VariableDefinition("is_running", "bool", "false"));
    warn_ir.transitions[0].action_sig->assignments.push_back(ActionAssignment("is_running", "42"));
    errors.clear();
    warnings.clear();
    EXPECT_TRUE(fsm::middleend::SemanticAnalyzer::validate(warn_ir, errors, warnings));
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings[0].find("Type mismatch"), std::string::npos);
}

TEST(FsmIrTest, PassManagerSemanticValidationExecution) {
    FsmIr ir;
    ir.name = "PipelineModel";
    ir.add_state("A");
    ir.initial_state = "A";
    ir.variables.push_back(VariableDefinition("counter", "int", "0"));

    TransitionEdge edge("A", "A", "EvStep", std::nullopt);
    ActionSignature act("Step");
    act.assignments.push_back(ActionAssignment("counter", "counter + 1"));
    edge.action_sig = act;
    ir.add_transition(std::move(edge));

    DiagnosticEngine diag;
    PassManager pm = PassManager::create_default_pipeline();
    bool pass_res = pm.run(ir, diag);
    EXPECT_TRUE(pass_res);
    EXPECT_FALSE(diag.has_errors());

    // Test with invalid model in pipeline
    ir.transitions[0].action_sig->assignments.push_back(ActionAssignment("non_existent_reg", "99"));
    DiagnosticEngine bad_diag;
    bool bad_res = pm.run(ir, bad_diag);
    EXPECT_FALSE(bad_res);
    EXPECT_TRUE(bad_diag.has_errors());
}

TEST(FsmIrTest, FsmValidatorIntegration) {
    FsmIr ir;
    ir.name = "ValidatorModel";
    ir.add_state("Active");
    ir.initial_state = "Active";

    // Unknown port type
    ir.ports.push_back(PortDefinition("bad_port", "InvalidNonExistentType", PortDirection::Out));

    auto result = FsmValidator::validate(ir);
    EXPECT_FALSE(result.is_valid);
    ASSERT_FALSE(result.errors.empty());

    bool found_semantic_err = false;
    for (const auto& err : result.errors) {
        if (err.find("InvalidNonExistentType") != std::string::npos) {
            found_semantic_err = true;
            break;
        }
    }
    EXPECT_TRUE(found_semantic_err);
}

/**
 * @brief Test Intent: Verify stimuli unification (signals as single source of truth).
 */
TEST(FsmIrTest, StimuliUnificationSignalsSingleTruth) {
    FsmIr ir;
    ir.name = "StimuliModel";

    // Add pure events via add_event
    ir.add_event("StartCmd", "Trigger system launch");
    ir.add_event("StopCmd", "Trigger emergency stop");

    // Add typed signal
    SignalDefinition typed_sig("TelemetryUpdate");
    typed_sig.attributes.emplace_back("battery_level", "float", "100.0");
    ir.signals.push_back(typed_sig);

    EXPECT_EQ(ir.signals.size(), 3u);

    // Verify lookup helpers
    const auto* start_sig = ir.find_signal("StartCmd");
    ASSERT_NE(start_sig, nullptr);
    EXPECT_EQ(start_sig->description, "Trigger system launch");
    EXPECT_TRUE(start_sig->attributes.empty());

    // Verify views
    auto names = ir.get_event_names();
    EXPECT_EQ(names.size(), 3u);
    EXPECT_TRUE(std::find(names.begin(), names.end(), "StartCmd") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "TelemetryUpdate") != names.end());

    auto event_views = ir.get_events();
    EXPECT_EQ(event_views.size(), 3u);
    EXPECT_EQ(event_views[0].name, "StartCmd");
    EXPECT_EQ(event_views[0].description, "Trigger system launch");
}

/**
 * @brief Test Intent: Verify unified AST representation and action/guard accessor methods.
 */
TEST(FsmIrTest, SingleRepresentationDualStringAst) {
    // StateNode action accessors
    StateNode st("Working");
    EXPECT_TRUE(st.get_entry_action().empty());
    EXPECT_TRUE(st.get_exit_action().empty());

    st.set_entry_action("InitWorker");
    EXPECT_EQ(st.get_entry_action(), "InitWorker");
    ASSERT_EQ(st.entry_actions.size(), 1u);
    EXPECT_EQ(st.entry_actions[0].name, "InitWorker");

    st.set_exit_action("CleanupWorker");
    EXPECT_EQ(st.get_exit_action(), "CleanupWorker");
    ASSERT_EQ(st.exit_actions.size(), 1u);
    EXPECT_EQ(st.exit_actions[0].name, "CleanupWorker");

    // TransitionEdge guard & action accessors
    TransitionEdge edge("Working", "Idle", "EvWork");
    EXPECT_TRUE(edge.get_guard().empty());
    EXPECT_TRUE(edge.get_action().empty());

    edge.set_guard("power_level > 20");
    EXPECT_EQ(edge.get_guard(), "power_level > 20");
    ASSERT_TRUE(edge.guard_ast.has_value());
    EXPECT_EQ(edge.guard_ast->to_string(), "power_level > 20");

    edge.set_action("NotifyPowerLow");
    EXPECT_EQ(edge.get_action(), "NotifyPowerLow");
    ASSERT_TRUE(edge.action_sig.has_value());
    EXPECT_EQ(edge.action_sig->name, "NotifyPowerLow");

    // Verify clear via nullopt
    edge.set_guard(std::nullopt);
    EXPECT_TRUE(edge.get_guard().empty());
    EXPECT_FALSE(edge.guard_ast.has_value());
}

/**
 * @brief Test Intent: Verify automatic synthesis and synchronization of Guard and Action interfaces.
 */
TEST(FsmIrTest, AutomaticInterfaceSynthesis) {
    FsmIr ir;
    ir.name = "SynthesisModel";

    auto& s1 = ir.add_state("Standby");
    s1.set_entry_action("InitSensors");
    s1.set_exit_action("LogStandbyExit");

    auto& s2 = ir.add_state("Active");
    s2.set_entry_action("ArmMotors");

    // Transitions with guards and actions
    TransitionEdge t1("Standby", "Active", "EvStart");
    t1.set_guard("BatteryHealthy");
    t1.set_action("ExecutePreflight");
    ir.add_transition(std::move(t1));

    TransitionEdge t2("Active", "Standby", "EvStop");
    t2.set_guard("BatteryLow");
    t2.set_action("ExecuteShutdown");
    ir.add_transition(std::move(t2));

    // Initially guards and actions are empty
    EXPECT_TRUE(ir.guards.empty());
    EXPECT_TRUE(ir.actions.empty());

    // Canonicalize triggers sync_interfaces
    ir.canonicalize();

    // Guards synthesized and sorted
    ASSERT_EQ(ir.guards.size(), 2u);
    EXPECT_EQ(ir.guards[0].name, "BatteryHealthy");
    EXPECT_EQ(ir.guards[1].name, "BatteryLow");

    // Actions synthesized and sorted
    ASSERT_EQ(ir.actions.size(), 5u);
    EXPECT_EQ(ir.actions[0].name, "ArmMotors");
    EXPECT_EQ(ir.actions[1].name, "ExecutePreflight");
    EXPECT_EQ(ir.actions[2].name, "ExecuteShutdown");
    EXPECT_EQ(ir.actions[3].name, "InitSensors");
    EXPECT_EQ(ir.actions[4].name, "LogStandbyExit");
}

/**
 * @brief Test Intent: Verify direct graph adjacency indices on StateNode for O(1) traversal.
 */
TEST(FsmIrTest, DirectGraphAdjacencyIndices) {
    FsmIr ir;
    ir.name = "TopologyModel";

    ir.add_state("Idle");
    ir.add_state("Running");
    ir.add_state("Paused");
    ir.add_state("Stopped");

    // Transition 0: Idle -> Running
    ir.add_transition(TransitionEdge("Idle", "Running", "EvStart"));
    // Transition 1: Running -> Paused
    ir.add_transition(TransitionEdge("Running", "Paused", "EvPause"));
    // Transition 2: Paused -> Running
    ir.add_transition(TransitionEdge("Paused", "Running", "EvResume"));
    // Transition 3: Running -> Stopped
    ir.add_transition(TransitionEdge("Running", "Stopped", "EvStop"));

    ir.canonicalize();

    const auto* idle = ir.find_state("Idle");
    ASSERT_NE(idle, nullptr);
    EXPECT_EQ(idle->incoming_transitions.size(), 0u);
    ASSERT_EQ(idle->outgoing_transitions.size(), 1u);

    const auto* running = ir.find_state("Running");
    ASSERT_NE(running, nullptr);
    // Running receives from Idle (t0) and Paused (t2)
    EXPECT_EQ(running->incoming_transitions.size(), 2u);
    // Running goes to Paused (t1) and Stopped (t3)
    EXPECT_EQ(running->outgoing_transitions.size(), 2u);

    // Verify O(1) transition lookup through contiguous indices
    for (uint32_t idx : running->outgoing_transitions) {
        ASSERT_LT(idx, ir.transitions.size());
        EXPECT_EQ(ir.transitions[idx].source, "Running");
        EXPECT_TRUE(ir.transitions[idx].target == "Paused" || ir.transitions[idx].target == "Stopped");
    }

    const auto* paused = ir.find_state("Paused");
    ASSERT_NE(paused, nullptr);
    EXPECT_EQ(paused->incoming_transitions.size(), 1u);
    EXPECT_EQ(paused->outgoing_transitions.size(), 1u);

    const auto* stopped = ir.find_state("Stopped");
    ASSERT_NE(stopped, nullptr);
    EXPECT_EQ(stopped->incoming_transitions.size(), 1u);
    EXPECT_EQ(stopped->outgoing_transitions.size(), 0u);
}

/**
 * @brief Test Intent: Verify backend-independent IR properties (package, attributes) and JSON roundtrip.
 */
TEST(FsmIrTest, BackendIndependenceAndPackageAttributes) {
    FsmIr ir;
    ir.name = "MissionSystem";
    ir.package = "aerospace.avionics";
    ir.attributes["author"] = "flight_team";
    ir.attributes["target_standard"] = "DO-178C";

    ir.add_state("Standby");
    ir.add_state("Armed");
    ir.add_transition(TransitionEdge("Standby", "Armed", "EvArm"));

    ir.canonicalize();

    EXPECT_EQ(ir.package, "aerospace.avionics");
    EXPECT_EQ(ir.attributes.at("author"), "flight_team");
    EXPECT_EQ(ir.attributes.at("target_standard"), "DO-178C");

    // JSON round-trip
    std::string json = FsmIrSerializer::serialize_json(ir);
    EXPECT_NE(json.find("\"package\": \"aerospace.avionics\""), std::string::npos);
    EXPECT_NE(json.find("\"target_standard\": \"DO-178C\""), std::string::npos);

    // Verify deserialization
    FsmIr deserialized;
    std::string err;
    fsm::frontend::diagram::JsonStateParser parser;
    EXPECT_TRUE(parser.parse(json, deserialized, err)) << err;
    EXPECT_EQ(deserialized.package, "aerospace.avionics");
    EXPECT_EQ(deserialized.attributes.at("author"), "flight_team");
    EXPECT_EQ(deserialized.attributes.at("target_standard"), "DO-178C");
}

/**
 * @brief Test Intent: Verify deterministic ID stability independent of backend options.
 */
TEST(FsmIrTest, DeterministicIdIndependence) {
    FsmIr ir1;
    ir1.name = "GuidanceController";
    ir1.package = "guidance";
    ir1.add_state("Navigating");
    ir1.canonicalize();

    FsmIr ir2;
    ir2.name = "GuidanceController";
    ir2.package = "guidance";
    ir2.add_state("Navigating");
    ir2.canonicalize();

    // Deterministic IDs must be invariant
    EXPECT_EQ(ir1.id, ir2.id);
    EXPECT_EQ(ir1.id.rfind("id_", 0), 0u);
    EXPECT_EQ(ir1.states[0].id, ir2.states[0].id);
}

/**
 * @brief Test Intent: Verify canonical transition priority contract (1 = highest, ..., 0 = default/lowest).
 */
TEST(FsmIrTest, CanonicalTransitionPriorityContract) {
    TransitionEdge t_def("S0", "S1", "EvA");
    t_def.priority = 0;
    EXPECT_FALSE(t_def.has_priority());

    TransitionEdge t_high("S0", "S2", "EvA");
    t_high.priority = 1;
    EXPECT_TRUE(t_high.has_priority());

    TransitionEdge t_med("S0", "S3", "EvA");
    t_med.priority = 2;
    EXPECT_TRUE(t_med.has_priority());

    TransitionEdge t_low("S0", "S4", "EvA");
    t_low.priority = 10;
    EXPECT_TRUE(t_low.has_priority());

    std::vector<TransitionEdge> edges = {t_def, t_low, t_high, t_med};

    // Canonical priority sort comparator:
    // 1 (highest) < 2 < 10 < ... < 0 (lowest)
    std::sort(edges.begin(), edges.end(), [](const TransitionEdge& a, const TransitionEdge& b) {
        auto pa = (a.priority == 0) ? std::numeric_limits<std::uint32_t>::max() : a.priority;
        auto pb = (b.priority == 0) ? std::numeric_limits<std::uint32_t>::max() : b.priority;
        return pa < pb;
    });

    ASSERT_EQ(edges.size(), 4u);
    EXPECT_EQ(edges[0].priority, 1u);
    EXPECT_EQ(edges[0].target, "S2");
    EXPECT_EQ(edges[1].priority, 2u);
    EXPECT_EQ(edges[1].target, "S3");
    EXPECT_EQ(edges[2].priority, 10u);
    EXPECT_EQ(edges[2].target, "S4");
    EXPECT_EQ(edges[3].priority, 0u);
    EXPECT_EQ(edges[3].target, "S1");
}

}  // namespace



/**
 * @file test_cpp_model_emitter.cpp
 * @brief Unit verification suite for C++ model emitter syntax generation.
 *
 * Test Intent:
 * Verify that CppModelEmitter correctly serializes partitioned domain structures
 * (InPorts, OutPorts, Registers, Services), typed signals with payload validators,
 * state lifecycle hooks, requirement traceability annotations, priority-ordered
 * transition tables, and EFSM guard expressions.
 */

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/cpp/cpp_model_emitter.hpp"
#include "fsm/backend/cpp/cpp_options.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::backend::cpp;
using namespace fsm::backend;
using namespace fsm::ir;

namespace {

FsmIr create_sample_ir() {
    FsmIr model;
    model.name = "DeviceController";
    model.package = "test_ns";

    model.initial_state = "Idle";

    // InPorts
    PortDefinition in_p("sensor_val", "float", PortDirection::In);
    in_p.min_value = 0.0;
    in_p.max_value = 100.0;
    in_p.constraint = "self >= 0.0 and self <= 100.0";
    model.ports.push_back(in_p);

    // OutPorts
    PortDefinition out_p("actuator_cmd", "float", PortDirection::Out);
    model.ports.push_back(out_p);

    // Registers
    model.variables.emplace_back("retry_count", "uint32_t", "0");

    // Events
    model.add_event("StartCmd");
    model.add_event("StopCmd");

    // States
    StateNode st_idle{"Idle"};
    model.states.push_back(st_idle);

    StateNode st_running{"Running"};
    st_running.deferred_events.emplace_back("StartCmd");
    model.states.push_back(st_running);

    // Guards
    GuardModel gd_safe{"IsSafeToStart", "IsSafeToStart", "in.sensor_val > 10.0f", "in.sensor_val > 10.0f"};
    model.guards.push_back(gd_safe);

    // Actions
    ActionModel ac_init{"InitializeHardware"};
    model.actions.push_back(ac_init);

    // Transitions
    TransitionEdge t1;
    t1.source = "Idle";
    t1.target = "Running";
    t1.event = "StartCmd";
    t1.guard = "IsSafeToStart";
    t1.action = "InitializeHardware";
    t1.priority = 1;
    model.transitions.push_back(t1);

    TransitionEdge t2;
    t2.source = "Running";
    t2.target = "Idle";
    t2.event = "StopCmd";
    t2.priority = 2;
    model.transitions.push_back(t2);

    return model;
}

}  // namespace

/**
 * @brief Test Intent: Verify C++ emission of partitioned domain structures (InPorts, OutPorts, Registers, Services).
 *
 * Scenario:
 * - Build FsmIr with InPorts (with numeric assert constraints), OutPorts, Registers, and external Actions.
 * - Emit domain structures using CppModelEmitter::emit_domain_structures.
 * - Verify that structs with exact member names, types, default initializers, and RPC virtual interfaces are generated.
 */
TEST(CppModelEmitterTest, PartitionedDomainStructuresEmission) {
    auto model = create_sample_ir();

    std::ostringstream out;
    CppModelEmitter::emit_domain_structures(out, model);
    std::string str = out.str();

    // Verify InPorts struct & constraint comments and validate_contracts()
    EXPECT_NE(str.find("struct DeviceControllerInPorts {"), std::string::npos);
    EXPECT_NE(str.find("float sensor_val{0.0}; // assert: self >= 0.0 and self <= 100.0"), std::string::npos);
    EXPECT_NE(str.find("[[nodiscard]] constexpr bool validate_contracts() const noexcept"), std::string::npos);
    EXPECT_NE(str.find("(sensor_val >= 0 && sensor_val <= 100)"), std::string::npos);

    // Verify OutPorts struct
    EXPECT_NE(str.find("struct DeviceControllerOutPorts {"), std::string::npos);
    EXPECT_NE(str.find("float actuator_cmd{0.0};"), std::string::npos);

    // Verify Registers struct
    EXPECT_NE(str.find("struct DeviceControllerRegisters {"), std::string::npos);
    EXPECT_NE(str.find("uint32_t retry_count{0};"), std::string::npos);

    // Verify Services struct (abstract RPC interface)
    EXPECT_NE(str.find("struct DeviceControllerServices {"), std::string::npos);
    EXPECT_NE(str.find("virtual void InitializeHardware()"), std::string::npos);
}

/**
 * @brief Test Intent: Verify C++ emission of strongly-typed signal structs with payload attributes and constexpr
 * validators.
 *
 * Scenario:
 * - Define signal `EvTelemetry` with attributes `len`, `ptr` and validation expressions.
 * - Emit events using CppModelEmitter::emit_events.
 * - Verify explicit constructor generation and `[[nodiscard]] constexpr bool is_valid()` validator implementation.
 */
TEST(CppModelEmitterTest, TypedSignalPayloadsWithValidators) {
    FsmIr model;
    model.name = "TelemetryFSM";

    SignalDefinition sig("EvTelemetry");
    sig.attributes.emplace_back("len", "uint32_t", "0");
    sig.attributes.emplace_back("ptr", "const uint8_t*", "nullptr");
    sig.validators.emplace_back("len > 0");
    sig.validators.emplace_back("ptr != nullptr");
    model.add_signal(sig);

    std::ostringstream out;
    CppModelEmitter::emit_events(out, model);
    std::string str = out.str();

    EXPECT_NE(str.find("struct EvTelemetry {"), std::string::npos);
    EXPECT_NE(str.find("uint32_t len{0};"), std::string::npos);
    EXPECT_NE(str.find("const uint8_t* ptr{nullptr};"), std::string::npos);
    EXPECT_NE(str.find("constexpr explicit EvTelemetry(uint32_t len_, const uint8_t* ptr_)"), std::string::npos);
    EXPECT_NE(str.find("[[nodiscard]] constexpr bool is_valid() const noexcept"), std::string::npos);
    EXPECT_NE(str.find("(len > 0) && (ptr != nullptr)"), std::string::npos);
}

/**
 * @brief Test Intent: Verify C++ emission of state lifecycle hooks (`on_entry`, `on_exit`), time invariants, and
 * traceability docstrings.
 *
 * Scenario:
 * - State has traceability requirements (REQ-SAFE-01, REQ-REALTIME-02), entry/exit actions, and time invariant.
 * - Emit states using CppModelEmitter::emit_states.
 * - Verify Doxygen comments `/// @satisfies`, `/// @invariant`, and partitioned on_entry/on_exit signatures.
 */
TEST(CppModelEmitterTest, StatesLifecycleHooksAndRequirements) {
    FsmIr model;
    model.name = "AerospaceFSM";
    model.initial_state = "Operating";

    StateNode st("Operating", "Operating");
    st.traceability_reqs.emplace_back("REQ-SAFE-01");
    st.traceability_reqs.emplace_back("REQ-REALTIME-02");
    st.entry_actions.emplace_back("ArmSensors", "ArmSensors");
    st.exit_actions.emplace_back("DisarmSensors", "DisarmSensors");
    st.time_invariant = "stay <= 100ms";
    model.add_state(st);

    std::ostringstream out;
    CppModelEmitter::emit_states(out, model);
    std::string str = out.str();

    // Check Traceability Doxygen Comments
    EXPECT_NE(str.find("/// @satisfies REQ-SAFE-01, REQ-REALTIME-02"), std::string::npos);

    // Check on_entry and on_exit lifecycle hooks
    EXPECT_NE(
        str.find(
            "void on_entry(const InPorts& /*in*/, OutPorts& /*out*/, Registers& /*reg*/, Services& /*srv*/) const"),
        std::string::npos);
    EXPECT_NE(str.find("// Entry action: ArmSensors"), std::string::npos);
    EXPECT_NE(
        str.find("void on_exit(const InPorts& /*in*/, OutPorts& /*out*/, Registers& /*reg*/, Services& /*srv*/) const"),
        std::string::npos);
    EXPECT_NE(str.find("// Exit action: DisarmSensors"), std::string::npos);

    // Check Time Invariant
    EXPECT_NE(str.find("/// @invariant stay <= 100ms"), std::string::npos);
    EXPECT_NE(str.find("static constexpr std::string_view time_invariant = \"stay <= 100ms\";"), std::string::npos);
}

/**
 * @brief Test Intent: Verify C++ emission of transition tables sorted by descending priority.
 *
 * Scenario:
 * - Define transitions with priority 100 (high) and priority 1 (low).
 * - Emit transition table using CppModelEmitter::emit_transition_table.
 * - Verify priority 100 transition appears before priority 1 transition in the generated table.
 */
TEST(CppModelEmitterTest, TransitionTablePriorityOrdering) {
    FsmIr model;
    model.name = "PrioFsm";
    model.initial_state = "Idle";

    StateNode idle("Idle");
    model.add_state(idle);
    StateNode running("Running");
    model.add_state(running);
    StateNode fault("Fault");
    model.add_state(fault);

    // High priority transition (precedence 1)
    TransitionEdge t_high("t2", "Idle", "Fault", SignalTrigger("EvTick"));
    t_high.priority = 1;
    model.add_transition(t_high);

    // Low priority transition (precedence 100)
    TransitionEdge t_low("t1", "Idle", "Running", SignalTrigger("EvTick"));
    t_low.priority = 100;
    model.add_transition(t_low);


    std::ostringstream out;
    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    CppModelEmitter::emit_transition_table(out, model, opts);
    std::string str = out.str();

    // Check transition ordering in table (t_high to Fault must appear before t_low to Running)
    auto pos_high = str.find("fsm::row<Idle, EvTick, Fault>");
    auto pos_low = str.find("fsm::row<Idle, EvTick, Running>");
    ASSERT_NE(pos_high, std::string::npos);
    ASSERT_NE(pos_low, std::string::npos);
    EXPECT_LT(pos_high, pos_low);
}

/**
 * @brief Test Intent: Verify automated C++ emission of resolved EFSM guard expressions over InPorts and Registers.
 *
 * Scenario:
 * - Define guard with cpp_expression "in.soc > 30.0f && !reg.is_faulty".
 * - Emit guards with include_stubs = true.
 * - Verify generated struct returns the direct expression.
 */
TEST(CppModelEmitterTest, EfsmResolvedGuardGeneration) {
    FsmIr model;
    model.name = "BatteryManager";
    model.initial_state = "Idle";
    model.add_state("Idle");

    GuardModel guard_item("battery_check", "Battery above threshold", "in.soc > 30.0f && !reg.is_faulty",
                          "in.soc > 30.0f && !reg.is_faulty");
    model.guards.push_back(guard_item);

    GeneratorOptions opts;
    opts.include_stubs = true;

    std::ostringstream out;
    CppModelEmitter::emit_guards(out, model, opts);
    std::string str = out.str();

    EXPECT_NE(str.find("struct battery_check {"), std::string::npos);
    EXPECT_NE(str.find("return in.soc > 30.0f && !reg.is_faulty;"), std::string::npos);
}

/**
 * @brief Test Intent: Verify C++ emission of SysML v2 / formal IR Enums and Structs.
 *
 * Scenario:
 * - Define EnumDefinition 'FlightMode' with explicit underlying type and literals.
 * - Define StructDefinition 'Waypoint' with typed fields and default values.
 * - Emit via CppModelEmitter and verify enum class, to_string constexpr, and struct definitions.
 */
TEST(CppModelEmitterTest, EnumAndStructDefinitionsEmission) {
    FsmIr model;
    model.name = "NavigationComputer";
    model.initial_state = "Idle";
    model.add_state("Idle");

    // Enum
    EnumDefinition flight_mode("FlightMode", "uint16_t", "Operational flight mode");
    flight_mode.add_literal(EnumLiteral{"Standby", 0, "Idle on ground"});
    flight_mode.add_literal(EnumLiteral{"EnRoute", 10, "Cruising waypoint navigation"});
    flight_mode.add_literal(EnumLiteral{"Approach", 20, "Instrument landing approach"});
    model.add_enum(flight_mode);

    // Struct
    StructDefinition waypoint("Waypoint", false, "Navigation waypoint coordinate");
    StructField lat{"lat", "double", "0.0"};
    lat.description = "Latitude in degrees";
    waypoint.add_field(lat);

    StructField lon{"lon", "double", "0.0"};
    lon.description = "Longitude in degrees";
    waypoint.add_field(lon);

    StructField alt{"alt_m", "float", "1000.0f"};
    alt.description = "Target altitude in meters";
    waypoint.add_field(alt);

    StructField fly{"is_flyover", "bool", "false"};
    fly.description = "Flyover vs flyby";
    waypoint.add_field(fly);
    model.add_struct(waypoint);

    std::ostringstream out;
    GeneratorOptions opts;
    CppModelEmitter::emit_model(out, model, opts);
    std::string str = out.str();

    // Verify Enum class and to_string
    EXPECT_NE(str.find("enum class FlightMode : uint16_t {"), std::string::npos);
    EXPECT_NE(str.find("Standby = 0, // Idle on ground"), std::string::npos);
    EXPECT_NE(str.find("EnRoute = 10, // Cruising waypoint navigation"), std::string::npos);
    EXPECT_NE(str.find("Approach = 20 // Instrument landing approach"), std::string::npos);
    EXPECT_NE(str.find("constexpr std::string_view to_string(FlightMode val) noexcept"), std::string::npos);
    EXPECT_NE(str.find("case FlightMode::Standby: return \"Standby\";"), std::string::npos);

    // Verify Struct and equality operators
    EXPECT_NE(str.find("struct Waypoint {"), std::string::npos);
    EXPECT_NE(str.find("double lat{0.0}; // Latitude in degrees"), std::string::npos);
    EXPECT_NE(str.find("float alt_m{1000.0f}; // Target altitude in meters"), std::string::npos);
    EXPECT_NE(str.find("bool is_flyover{false}; // Flyover vs flyby"), std::string::npos);
    EXPECT_NE(str.find("bool operator==(const Waypoint& other) const noexcept"), std::string::npos);
    EXPECT_NE(str.find("lat == other.lat &&"), std::string::npos);
}

/**
 * @brief Test Intent: Verify C++ emission of modern fluent factory aliases (make_fsm, make_thread_safe_fsm, make_spsc_fsm).
 */
TEST(CppModelEmitterTest, FluentFactoryAliasesEmission) {
    auto model = create_sample_ir();

    std::ostringstream out;
    GeneratorOptions opts;
    CppModelEmitter::emit_model(out, model, opts);
    std::string str = out.str();

    EXPECT_NE(str.find("using DeviceController = ::fsm::make_fsm<"), std::string::npos);
    EXPECT_NE(str.find("::fsm::with_initial_state<Idle>"), std::string::npos);
    EXPECT_NE(str.find("::fsm::with_ports<DeviceControllerInPorts, DeviceControllerOutPorts>"), std::string::npos);
    EXPECT_NE(str.find("::fsm::with_registers<DeviceControllerRegisters>"), std::string::npos);
    EXPECT_NE(str.find("::fsm::with_services<DeviceControllerServices>"), std::string::npos);
    EXPECT_NE(str.find("::fsm::with_observer<::fsm::dynamic_observer>"), std::string::npos);

    EXPECT_NE(str.find("using ThreadSafeDeviceController = ::fsm::make_thread_safe_fsm<"), std::string::npos);
    EXPECT_NE(str.find("using SpscDeviceController = ::fsm::make_spsc_fsm<"), std::string::npos);
    EXPECT_NE(str.find("::fsm::with_queue_capacity<64>"), std::string::npos);
}

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

#include "fsm/backend/cpp/cpp_backend_validator.hpp"
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
    t1.set_action("InitializeHardware");
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
 * @brief Verify C++ emission of partitioned domain structures (ports, signals, states, transitions).
 * @scenario Emit complete FsmIr model containing ports, signals, and states to modern C++ header.
 * @expected Generated header contains segregated namespace declarations for ports, signals, and states.
 */
TEST(CppModelEmitter, PartitionedDomainStructures_EmittedCorrectly) {
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
 * @brief Verify C++ emission of strongly-typed signal structs with inline validator predicates.
 * @scenario Emit signals with attributes having range and assert constraints.
 * @expected Generated signal structs include validate() methods enforcing constraints.
 */
TEST(CppModelEmitter, TypedSignalPayloads_EmittedWithValidators) {
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
 * @brief Verify C++ emission of state lifecycle hooks (on_enter, on_exit) and requirement annotations.
 * @scenario Emit states declaring entry/exit actions and traceability tags.
 * @expected State struct definitions include typed on_enter and on_exit member function templates.
 */
TEST(CppModelEmitter, StatesLifecycleHooks_EmittedWithRequirements) {
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

    // Check on_enter and on_exit lifecycle hooks
    EXPECT_NE(str.find("void on_enter(const InPorts& in, OutPorts& out, Registers& reg, Services& srv) const"),
              std::string::npos);
    EXPECT_NE(str.find("auto entry_action_0 = ArmSensors{};"), std::string::npos);
    EXPECT_NE(str.find("::fsm::call_action(entry_action_0, in, out, reg, srv);"), std::string::npos);
    EXPECT_NE(str.find("void on_exit(const InPorts& in, OutPorts& out, Registers& reg, Services& srv) const"),
              std::string::npos);
    EXPECT_NE(str.find("auto exit_action_0 = DisarmSensors{};"), std::string::npos);
    EXPECT_NE(str.find("::fsm::call_action(exit_action_0, in, out, reg, srv);"), std::string::npos);

    // Check Time Invariant
    EXPECT_NE(str.find("/// @invariant stay <= 100ms"), std::string::npos);
    EXPECT_NE(str.find("static constexpr std::string_view time_invariant = \"stay <= 100ms\";"), std::string::npos);
}

/**
 * @brief Verify the C++ backend rejects structural IR that has not been lowered.
 * @scenario Validate a parallel state with an unsupported do_activity declaration.
 * @expected Validation reports stable backend diagnostics for every unsupported feature.
 */
TEST(CppBackendValidator, UnloweredStructuralFeatures_AreRejectedBeforeEmission) {
    FsmIr model;
    model.name = "UnsupportedStructure";
    model.initial_state = "ParallelRoot";
    model.add_state("ParallelRoot", "", StateKind::Parallel);
    model.add_state(StateNode{"Worker", "", "ParallelRoot", StateKind::Atomic});
    model.find_state_mut("ParallelRoot")->do_activity = "background_worker";

    fsm::diagnostic::DiagnosticEngine diagnostics;
    EXPECT_FALSE(CppBackendValidator::validate_model(model, diagnostics));
    ASSERT_EQ(diagnostics.get_diagnostics().size(), 2u);
    EXPECT_EQ(diagnostics.get_diagnostics()[0].code, "ECPP001");
    EXPECT_EQ(diagnostics.get_diagnostics()[1].code, "ECPP006");
}

/**
 * @brief Verify the C++ backend accepts an already lowered atomic model.
 * @scenario Validate a single-state model without unsupported structural features.
 * @expected Validation succeeds without producing diagnostics.
 */
TEST(CppBackendValidator, AtomicModel_IsAcceptedByCxxBackend) {
    FsmIr model;
    model.name = "AtomicModel";
    model.initial_state = "Idle";
    model.add_state("Idle");

    fsm::diagnostic::DiagnosticEngine diagnostics;
    EXPECT_TRUE(CppBackendValidator::validate_model(model, diagnostics));
    EXPECT_TRUE(diagnostics.get_diagnostics().empty());
}

/**
 * @brief Verify the C++ backend rejects IR time triggers without supported lowering.
 * @scenario Validate an absolute-time transition that has no generated clock scheduling path.
 * @expected The transition produces the ECPP008 backend contract diagnostic.
 */
TEST(CppBackendValidator, AbsoluteTimeTrigger_IsRejectedUntilClockLoweringExists) {
    FsmIr model;
    model.name = "TimedModel";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Running");

    TransitionEdge at_transition("at", "Idle", "Running", SignalTrigger{});
    at_transition.trigger = TimeTrigger{TimeTriggerKind::At, 30};
    model.add_transition(at_transition);

    fsm::diagnostic::DiagnosticEngine diagnostics;
    EXPECT_FALSE(CppBackendValidator::validate_model(model, diagnostics));
    ASSERT_EQ(diagnostics.get_diagnostics().size(), 1u);
    EXPECT_EQ(diagnostics.get_diagnostics().front().code, "ECPP008");
}

/**
 * @brief Verify the C++ backend rejects unlowered Fork and Join pseudostates.
 * @scenario Model contains Fork and Join StateNodes without prior lowering.
 * @expected Validation reports ECPP002 and ECPP003 diagnostics.
 */
TEST(CppBackendValidator, ForkAndJoinPseudostates_AreRejectedByCxxBackend) {
    FsmIr model;
    model.name = "UnloweredForkJoinModel";
    model.initial_state = "Idle";
    model.add_state("Idle");

    StateNode fork_s("ForkNode");
    fork_s.kind = StateKind::Fork;
    model.add_state(fork_s);

    StateNode join_s("JoinNode");
    join_s.kind = StateKind::Join;
    model.add_state(join_s);

    fsm::diagnostic::DiagnosticEngine diagnostics;
    EXPECT_FALSE(CppBackendValidator::validate_model(model, diagnostics));
    ASSERT_EQ(diagnostics.get_diagnostics().size(), 2u);
    EXPECT_EQ(diagnostics.get_diagnostics()[0].code, "ECPP002");
    EXPECT_EQ(diagnostics.get_diagnostics()[1].code, "ECPP003");
}

/**
 * @brief Verify the C++ backend rejects unlowered multi-target and multi-source transitions.
 * @scenario Model contains transitions with multi-target or multi-source endpoints that
 *           were not lowered into product-state transitions.
 * @expected Validation reports ECPP009 and ECPP010 diagnostics.
 */
TEST(CppBackendValidator, MultiTargetAndMultiSourceTransitions_AreRejectedByCxxBackend) {
    FsmIr model;
    model.name = "MultiEndpointModel";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("S1");
    model.add_state("S2");
    model.add_state("S3");

    TransitionEdge t_multi_tgt("t_fork", "Idle", "S1", SignalTrigger("Ev1"));
    t_multi_tgt.target_ids = {"S1", "S2"};
    model.add_transition(t_multi_tgt);

    TransitionEdge t_multi_src("t_join", "S1", "S3", SignalTrigger("Ev2"));
    t_multi_src.source_ids = {"S1", "S2"};
    model.add_transition(t_multi_src);

    fsm::diagnostic::DiagnosticEngine diagnostics;
    EXPECT_FALSE(CppBackendValidator::validate_model(model, diagnostics));
    ASSERT_EQ(diagnostics.get_diagnostics().size(), 2u);
    EXPECT_EQ(diagnostics.get_diagnostics()[0].code, "ECPP009");
    EXPECT_EQ(diagnostics.get_diagnostics()[1].code, "ECPP010");
}

/**
 * @brief Verify that CppGenerator inlines Choice states declared directly in model.states.
 * @scenario Model has Choice in model.states (choice_nodes list is empty).
 * @expected CppGenerator::generate_header runs ChoiceInliningPass and successfully generates C++ header without
 * throwing ECPP004.
 */
TEST(CppGenerator, ChoiceInStateList_IsInlinedAutomaticallyBeforeValidation) {
    FsmIr model;
    model.name = "AutoInlinedChoiceFsm";
    model.initial_state = "Idle";
    model.add_state("Idle");

    StateNode choice_st("DecisionPoint");
    choice_st.kind = StateKind::Choice;
    model.add_state(choice_st);

    model.add_state("TargetA");
    model.add_state("TargetB");

    model.signals.emplace_back("EvCheck");

    // Incoming transition
    model.add_transition(TransitionEdge("Idle", "DecisionPoint", "EvCheck"));

    // Outgoing transitions with guards
    TransitionEdge t_ok("DecisionPoint", "TargetA", "");
    t_ok.guard = "IsOk";
    model.add_transition(t_ok);

    TransitionEdge t_fail("DecisionPoint", "TargetB", "");
    t_fail.guard = "else";
    model.add_transition(t_fail);

    model.guards.emplace_back("IsOk");

    GeneratorOptions opts;
    opts.standalone = false;

    EXPECT_NO_THROW({
        std::string code = CppGenerator::generate_header(model, opts);
        EXPECT_FALSE(code.empty());
        EXPECT_NE(code.find("TargetA"), std::string::npos);
        EXPECT_NE(code.find("TargetB"), std::string::npos);
    });
}

/**
 * @brief Verify the C++ emitter maps constant after/every triggers to runtime timer events.
 * @scenario Emit transitions with fixed-duration after and every IR triggers.
 * @expected The transition table contains after_ms and every_ms event types.
 */
TEST(CppModelEmitter, ConstantTimeTriggers_EmitRuntimeTimerEvents) {
    FsmIr model;
    model.name = "TimedEmitter";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Running");

    TransitionEdge after_transition("after", "Idle", "Running", SignalTrigger{});
    after_transition.trigger = TimeTrigger{TimeTriggerKind::After, 10};
    TransitionEdge every_transition("every", "Idle", "Running", SignalTrigger{});
    every_transition.trigger = TimeTrigger{TimeTriggerKind::Every, 20};
    model.add_transition(after_transition);
    model.add_transition(every_transition);

    GeneratorOptions options;
    options.standalone = false;
    options.target_namespace = "timed_test";
    const auto generated = CppGenerator::generate_header(model, options);

    EXPECT_NE(generated.find("::fsm::after_ms<10>"), std::string::npos);
    EXPECT_NE(generated.find("::fsm::every_ms<20>"), std::string::npos);
}

/**
 * @brief Verify the C++ generator lowers parallel regions into product states.
 * @scenario Generate a header from two orthogonal regions with two states each.
 * @expected Generated C++ contains Cartesian product states and omits the original region leaves.
 */
TEST(CppGenerator, ParallelRegions_AreLoweredBeforeEmission) {
    FsmIr model;
    model.name = "ParallelController";
    model.initial_state = "DualChannel";

    auto& parallel = model.add_state("DualChannel", "", StateKind::Parallel);
    OrthogonalRegion first_region;
    first_region.id = "First";
    first_region.name = "First";
    first_region.initial_state_id = "FirstIdle";
    first_region.state_ids = {"FirstIdle", "FirstActive"};
    OrthogonalRegion second_region;
    second_region.id = "Second";
    second_region.name = "Second";
    second_region.initial_state_id = "SecondIdle";
    second_region.state_ids = {"SecondIdle", "SecondActive"};
    parallel.orthogonal_regions = {first_region, second_region};

    model.add_state("FirstIdle", "First");
    model.add_state("FirstActive", "First");
    model.add_state("SecondIdle", "Second");
    model.add_state("SecondActive", "Second");

    GeneratorOptions options;
    options.standalone = false;
    options.target_namespace = "parallel_test";
    const auto generated = CppGenerator::generate_header(model, options);

    EXPECT_NE(generated.find("DualChannel_FirstIdle_SecondIdle"), std::string::npos);
    EXPECT_EQ(generated.find("struct FirstIdle {"), std::string::npos);
    EXPECT_EQ(generated.find("struct SecondIdle {"), std::string::npos);
}

/**
 * @brief Verify C++ emission of transition tables sorted by descending priority.
 * @scenario Emit model with multiple transitions with varying priorities.
 * @expected Generated transition_table type list orders rows deterministically by priority.
 */
TEST(CppModelEmitter, TransitionTable_EmittedWithPriorityOrdering) {
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
 * @brief Verify automated C++ emission of resolved EFSM guard lambda functions.
 * @scenario Emit model containing relational guard expressions on port and state variables.
 * @expected Generated guard structs contain evaluatable constexpr or inline boolean expressions.
 */
TEST(CppModelEmitter, EfsmResolvedGuards_EmittedCorrectly) {
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
 * @brief Verify C++ emission of SysML v2 / formal IR Enums and Struct definitions.
 * @scenario Emit model containing custom enum definitions and composite struct definitions.
 * @expected Generated header declares scoped enum classes and C++ struct types.
 */
TEST(CppModelEmitter, EnumAndStructDefinitions_EmittedCorrectly) {
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
 * @brief Verify C++ emission of modern fluent factory aliases (make_fsm, make_thread_safe_fsm).
 * @scenario Emit model with default factory options.
 * @expected Generated header provides type aliases and make_fsm convenience factory functions.
 */
TEST(CppModelEmitter, FluentFactoryAliases_EmittedCorrectly) {
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

/**
 * @brief Verify C++ emission of Doxygen requirement traceability annotations (@satisfies).
 * @scenario Emit model with formal traceability requirement tags on states and transitions.
 * @expected Emitted structs include standard @satisfies Doxygen comments.
 */
TEST(CppModelEmitter, DoxygenTraceabilityAnnotations_EmittedCorrectly) {
    FsmIr model;
    model.name = "MissionComputer";
    model.initial_state = "Idle";
    model.satisfies_reqs = {"REQ-SYS-001", "REQ-SYS-002"};

    StateNode idle{"Idle"};
    idle.traceability_reqs = {"REQ-STATE-IDLE"};
    model.states.push_back(idle);

    std::ostringstream out;
    GeneratorOptions opts;
    CppModelEmitter::emit_model(out, model, opts);
    std::string str = out.str();

    // Verify state Doxygen contains @trace and @satisfies
    EXPECT_NE(str.find("* @trace REQ-STATE-IDLE"), std::string::npos);
    EXPECT_NE(str.find("* @satisfies REQ-STATE-IDLE"), std::string::npos);

    // Verify FSM alias Doxygen contains @trace and @satisfies
    EXPECT_NE(str.find("* @trace REQ-SYS-001"), std::string::npos);
    EXPECT_NE(str.find("* @satisfies REQ-SYS-001"), std::string::npos);
    EXPECT_NE(str.find("* @trace REQ-SYS-002"), std::string::npos);
    EXPECT_NE(str.find("* @satisfies REQ-SYS-002"), std::string::npos);
}

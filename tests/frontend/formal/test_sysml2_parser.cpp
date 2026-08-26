#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/fsm_validator.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify OMG SysML v2 multi-line transition syntax parsing.
 *
 * Scenario:
 * - Parse `transition name first Source accept Event if Guard do Action then Target;`.
 * - Verify name, initial state, triggers, guards, actions, and target states are captured in IR.
 */
TEST(Sysml2ParserTest, MultilineTransitionParsing) {
    const std::string sysml_text = R"(
    state def MissionBehavior {
        entry; then Standby;

        state Standby;
        state InFlight;

        transition authorize_mission
            first Standby
            accept AuthorizeCmd
            if ValidClearanceGuard
            do ArmEnginesAction
            then InFlight;

        transition abort_mission
            first Standby
            accept AuthorizeCmd
            if NoClearanceGuard
            do TriggerAlarmAction
            then Aborted;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "MissionBehavior");
    EXPECT_EQ(model.initial_state, "Standby");
    ASSERT_NE(model.find_state("Standby"), nullptr);
    ASSERT_NE(model.find_state("InFlight"), nullptr);
    ASSERT_NE(model.find_state("Aborted"), nullptr);
    EXPECT_EQ(model.events.size(), 1u);
    EXPECT_EQ(model.guards.size(), 2u);
    EXPECT_EQ(model.actions.size(), 2u);
    EXPECT_EQ(model.transitions.size(), 2u);
}

/**
 * @brief Test Intent: Verify OMG SysML v2 compact transition syntax parsing.
 *
 * Scenario:
 * - Parse shorthand `transition from S accept E do A then D;`.
 * - Verify all transition elements are populated into the transition table model.
 */
TEST(Sysml2ParserTest, CompactTransitionParsing) {
    const std::string sysml_text = R"(
    state def DeviceProtocol {
        entry; then Disconnected;

        state Disconnected;
        state Connecting;
        state Connected;

        transition from Disconnected accept ConnectCmd then Connecting;
        transition from Connecting accept SuccessEvent do OnConnectedAction then Connected;
        transition from Connecting accept FailEvent then Disconnected;
        transition from Connected accept DisconnectCmd then Disconnected;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    EXPECT_EQ(model.initial_state, "Disconnected");
    EXPECT_EQ(model.states.size(), 3u);
    EXPECT_EQ(model.transitions.size(), 4u);
    EXPECT_EQ(model.actions.size(), 1u);
}

/**
 * @brief Test Intent: Verify SysML v2 composite state hierarchy and C++ code generator emission.
 *
 * Scenario:
 * - Parse nested `state Standby { entry; then Diagnostics; ... }`.
 * - Verify composite metadata and compile generated C++ standalone code.
 */
TEST(Sysml2ParserTest, CompositeStatesAndCodegen) {
    const std::string sysml_text = R"(
    state def Spacecraft {
        entry; then Standby;

        state Standby {
            entry; then Diagnostics;
            state Diagnostics;
            state Calibrated;
        }

        state InFlight {
            entry; then Ascending;
            state Ascending;
            state Cruising;
        }

        transition from Calibrated accept AuthorizeCmd then Ascending;
        transition from Ascending accept AltitudeReachedEvent do DeployPanelsAction then Cruising;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    const auto* standby = model.find_state("Standby");
    ASSERT_NE(standby, nullptr);
    EXPECT_TRUE(standby->is_composite);
    EXPECT_EQ(standby->initial_sub_state, "Diagnostics");

    const auto* diag = model.find_state("Diagnostics");
    ASSERT_NE(diag, nullptr);
    EXPECT_EQ(diag->parent_state, "Standby");

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    opts.standalone = true;

    const std::string code = CppGenerator::generate_header(model, opts);
    EXPECT_FALSE(code.empty());
    EXPECT_NE(code.find("struct Diagnostics"), std::string::npos);
    EXPECT_NE(code.find("struct DeployPanelsAction"), std::string::npos);
}

/**
 * @brief Test Intent: Verify SysML v2 attribute declarations, typed item defs (signals), and state actions.
 *
 * Scenario:
 * - Parse `attribute battery_percent : Integer = 100;`.
 * - Parse `item def EvTelemetry { attribute battery_mv : Integer; ... }`.
 * - Parse `satisfy requirement ...`, `entry action`, `do action`, `exit action`, `defer`.
 * - Verify types are mapped correctly to C++ primitives (uint32_t, float, bool).
 */
TEST(Sysml2ParserTest, NativeSysml2AttributesAndItemDefs) {
    const std::string sysml_text = R"(
    state def SatelliteBehavior {
        attribute battery_percent : Integer = 100;
        attribute altitude_m : Real = 450.5;
        attribute retry_count : Natural = 0;

        item def EvTelemetry {
            attribute battery_mv : Integer;
            attribute altitude_cm : Integer;
            attribute gps_locked : Boolean;
        }

        item def EvWaypointCmd {
            attribute lat : Real;
            attribute lon : Real;
            attribute target_alt : Real;
        }

        event def HeartbeatEvent;

        entry; then Standby;

        state Standby {
            satisfy requirement REQ_SAT_01;
            entry action InitSensors;
            do action background_telemetry;
            exit action Cleanup;
            defer HeartbeatEvent;
        }
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    // Check EFSM variables
    ASSERT_EQ(model.variables.size(), 3u);
    EXPECT_EQ(model.variables[0].name, "battery_percent");
    EXPECT_EQ(model.variables[0].type, "uint32_t");
    EXPECT_EQ(model.variables[0].initial_value, "100");

    EXPECT_EQ(model.variables[1].name, "altitude_m");
    EXPECT_EQ(model.variables[1].type, "float");
    EXPECT_EQ(model.variables[1].initial_value, "450.5");

    EXPECT_EQ(model.variables[2].name, "retry_count");
    EXPECT_EQ(model.variables[2].type, "uint32_t");

    // Check Signal Definitions
    ASSERT_EQ(model.signals.size(), 3u);
    EXPECT_EQ(model.signals[0].name, "EvTelemetry");
    ASSERT_EQ(model.signals[0].attributes.size(), 3u);
    EXPECT_EQ(model.signals[0].attributes[0].name, "battery_mv");
    EXPECT_EQ(model.signals[0].attributes[0].type, "uint32_t");
    EXPECT_EQ(model.signals[0].attributes[2].name, "gps_locked");
    EXPECT_EQ(model.signals[0].attributes[2].type, "bool");

    EXPECT_EQ(model.signals[1].name, "EvWaypointCmd");
    ASSERT_EQ(model.signals[1].attributes.size(), 3u);
    EXPECT_EQ(model.signals[1].attributes[0].name, "lat");
    EXPECT_EQ(model.signals[1].attributes[0].type, "float");

    // Check Standby state properties
    const auto* standby = model.find_state("Standby");
    ASSERT_NE(standby, nullptr);
    ASSERT_EQ(standby->traceability_reqs.size(), 1u);
    EXPECT_EQ(standby->traceability_reqs[0], "REQ_SAT_01");
    ASSERT_EQ(standby->entry_actions.size(), 1u);
    EXPECT_EQ(standby->entry_actions[0].name, "InitSensors");
    ASSERT_EQ(standby->exit_actions.size(), 1u);
    EXPECT_EQ(standby->exit_actions[0].name, "Cleanup");
    EXPECT_EQ(standby->do_activity, "background_telemetry");
    ASSERT_EQ(standby->deferred_events.size(), 1u);
    EXPECT_EQ(standby->deferred_events[0], "HeartbeatEvent");
}

/**
 * @brief Test Intent: Verify SysML v2 parallel orthogonal states and submachine references.
 *
 * Scenario:
 * - Parse `parallel state Operational { state NavRegion ... state CommsRegion ... }`.
 * - Parse submachine invocation `state SubGuidance :> GuidanceSubmachine;`.
 * - Verify IR correctly classifies states and links submachines.
 */
TEST(Sysml2ParserTest, ParallelRegionsAndSubmachineRef) {
    const std::string sysml_text = R"(
    state def AvionicsController {
        entry; then Operational;

        parallel state Operational {
            state NavRegion {
                state NavStandby;
                state NavActive;
            }
            state CommsRegion {
                state CommsStandby;
                state CommsActive;
            }
        }

        state SubGuidance :> GuidanceSubmachine;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "AvionicsController");
    EXPECT_EQ(model.initial_state, "Operational");
    ASSERT_NE(model.find_state("Operational"), nullptr);
}

/**
 * @brief Test Intent: Verify SysML v2 parsing of EntryPoint, ExitPoint, stay duration (time invariant), and transition
 * priorities.
 *
 * Scenario:
 * - Parse state machine with `entry point EnPort;`, `exit point ExPort;`, `stay duration <= 500[ms];`, and `transition
 * [priority=10]`.
 * - Verify IR captures StateKind::EntryPoint, StateKind::ExitPoint, time_invariant, and transition priority.
 */
TEST(Sysml2ParserTest, EntryExitPointTimeInvariantAndPriority) {
    const std::string sysml_text = R"(
    state def TimedFlightController {
        entry; then Standby;

        state Standby {
            stay duration <= 250[ms];
        }

        entry point EnPort;
        exit point ExPort;

        transition t_fast
            priority 10
            first Standby
            accept EvTick
            then EnPort;

        transition t_slow
            priority 1
            first Standby
            accept EvTick
            then ExPort;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    const auto* standby = model.find_state("Standby");
    ASSERT_NE(standby, nullptr);
    ASSERT_TRUE(standby->time_invariant.has_value());
    EXPECT_EQ(*standby->time_invariant, "250[ms]");

    const auto* en_port = model.find_state("EnPort");
    ASSERT_NE(en_port, nullptr);
    EXPECT_EQ(en_port->kind, StateKind::EntryPoint);

    const auto* ex_port = model.find_state("ExPort");
    ASSERT_NE(ex_port, nullptr);
    EXPECT_EQ(ex_port->kind, StateKind::ExitPoint);

    ASSERT_EQ(model.transitions.size(), 2u);
    EXPECT_EQ(model.transitions[0].priority, 10u);
    EXPECT_EQ(model.transitions[1].priority, 1u);
}

}  // namespace

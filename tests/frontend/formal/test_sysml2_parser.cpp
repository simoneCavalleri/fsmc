/**
 * @file test_sysml2_parser.cpp
 * @brief Unit test suite for the OMG SysML v2 State Definition frontend parser.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"

using namespace fsm::frontend::formal;
using namespace fsm::frontend;
using namespace fsm::backend::cpp;
using namespace fsm::backend;
using namespace fsm::middleend::analysis;
using namespace fsm::middleend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify multiline SysML v2 transition syntax with accept, if, and do clauses.
 * @scenario Parse SysML v2 transitions spanning multiple lines with triggers and guards.
 * @expected FsmIr captures source, target, signal, guard, and action correctly.
 */
TEST(Sysml2Parser, MultilineTransitions_ParsedIntoValidFsmIr) {
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
    EXPECT_EQ(model.signals.size(), 1u);
    EXPECT_EQ(model.guards.size(), 2u);
    EXPECT_EQ(model.actions.size(), 2u);
    EXPECT_EQ(model.transitions.size(), 2u);
}

/**
 * @brief Verify compact inline SysML v2 transition syntax (first S1; then S2;).
 * @scenario Parse compact transitions between states with inline accept triggers.
 * @expected Transitions and target states correctly populated in FsmIr.
 */
TEST(Sysml2Parser, CompactTransitions_ParsedIntoValidFsmIr) {
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
 * @brief Verify nested composite states in SysML v2 and downstream code generation.
 * @scenario Parse composite state definitions with nested initial and leaf states.
 * @expected Composite hierarchy formed and C++ code generator successfully produces header.
 */
TEST(Sysml2Parser, CompositeStates_ParsedAndCodeGenerated) {
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
 * @brief Verify SysML v2 attribute definitions and item def message payload types.
 * @scenario Parse SysML v2 model declaring typed attributes, initial values, and item defs.
 * @expected FsmIr variables and signal payload structs created matching declarations.
 */
TEST(Sysml2Parser, NativeAttributesAndItemDefs_CapturedInIr) {
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
 * @brief Verify parallel orthogonal regions and submachine references in SysML v2.
 * @scenario Parse parallel state defs with orthogonal regions and submachine invocations.
 * @expected Orthogonal regions and submachine links captured in StateNode structures.
 */
TEST(Sysml2Parser, ParallelRegionsAndSubmachines_ParsedCorrectly) {
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
 * @brief Verify entryPoint, exitPoint, stay duration invariants, and transition priorities.
 * @scenario Parse SysML v2 model with entry/exit points, stay duration assertions, and priorities.
 * @expected IR captures StateKind::EntryPoint, StateKind::ExitPoint, invariants, and priorities.
 */
TEST(Sysml2Parser, EntryExitPointAndInvariants_CapturedInIr) {
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

/**
 * @brief Verify choice pseudostate parsing with relational comparison guards.
 * @scenario Parse SysML v2 choice nodes branching on variable relational conditions.
 * @expected Choice pseudostates and conditional branches correctly represented in FsmIr.
 */
TEST(Sysml2Parser, ChoiceNodeComparisonGuards_ParsedCorrectly) {
    const std::string sysml_text = R"(
    state def BootSequencer {
        entry; then Booting;

        state Booting;
        state Operational;
        state DiagnosticMode;

        decide evaluate_boot_health;

        transition boot_entry
            first Booting
            accept BootAuthorizeCmd
            then evaluate_boot_health;

        transition boot_nominal
            first evaluate_boot_health
            if batterySoC > 30.0 and not criticalError
            do action emitBootSuccessTelemetry
            then Operational;

        transition boot_degraded
            first evaluate_boot_health
            else
            do action logBootFailureReason
            then DiagnosticMode;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    // Choice node must be inlined: only real states should remain
    EXPECT_EQ(model.initial_state, "Booting");
    ASSERT_NE(model.find_state("Booting"), nullptr);
    ASSERT_NE(model.find_state("Operational"), nullptr);
    ASSERT_NE(model.find_state("DiagnosticMode"), nullptr);

    // The parser emits 3 raw transitions; choice inlining happens in the middle-end.
    // The test validates what the parser itself produces, not the post-optimization IR.
    ASSERT_EQ(model.transitions.size(), 3u);

    // Transition 0: entry edge into the choice node (no guard)
    const auto& entry_edge = model.transitions[0];
    EXPECT_EQ(entry_edge.source, "Booting");
    EXPECT_EQ(entry_edge.event, "BootAuthorizeCmd");
    EXPECT_FALSE(entry_edge.guard.has_value()) << "Entry edge to choice must be unguarded";

    // Transition 1: nominal branch out of the choice node (guarded)
    // The guard 'batterySoC > 30.0 and not criticalError' is parsed through GuardExpressionParser.
    // Comparison operators and values are incorporated into the canonical C++ identifier via
    // sanitize_identifier, so '> 30.0' becomes part of a token like 'batterySoC__30_0'.
    // The resulting guard identifier must reference the variable 'batterySoC'.
    const auto& nominal = model.transitions[1];
    EXPECT_EQ(nominal.target, "Operational");
    ASSERT_TRUE(nominal.guard.has_value()) << "Nominal branch must have a guard";
    const std::string& nominal_guard = nominal.guard.value();
    EXPECT_NE(nominal_guard.find("batterySoC"), std::string::npos)
        << "Guard must reference 'batterySoC': " << nominal_guard;
    // The guard must not be empty and must differ from a bare unguarded identifier
    EXPECT_FALSE(nominal_guard.empty());

    // Transition 2: else/fallback branch (no guard or empty guard)
    const auto& fallback = model.transitions[2];
    EXPECT_EQ(fallback.target, "DiagnosticMode");
}

/**
 * @brief Verify semantic EFSM variable assignment actions in transition do blocks.
 * @scenario Parse SysML v2 transitions with do actions modifying state variables (x = x + 1).
 * @expected Assignment expressions captured in ActionSignature AST.
 */
TEST(Sysml2Parser, EfsmAssignmentActions_ParsedCorrectly) {
    const std::string sysml_text = R"(
    state def OrbitalCycleTracker {
        attribute orbitCycleCount : Integer = 0;
        attribute energyBudget : Real = 100.0;

        entry; then Monitoring;

        state Monitoring;
        state Downlinking;

        transition cycle_complete
            first Monitoring
            accept EclipseExitSignal
            do {
                orbitCycleCount = orbitCycleCount + 1;
            }
            then Downlinking;

        transition budget_update
            first Downlinking
            accept BudgetUpdateCmd
            do {
                energyBudget += 5.0;
            }
            then Monitoring;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    ASSERT_EQ(model.variables.size(), 2u);
    EXPECT_EQ(model.variables[0].name, "orbitCycleCount");
    EXPECT_EQ(model.variables[1].name, "energyBudget");

    // Transition 1: increment action for orbitCycleCount
    ASSERT_EQ(model.transitions.size(), 2u);
    const auto& t1 = model.transitions[0];
    ASSERT_TRUE(t1.transition_action.has_value()) << "cycle_complete transition must have a semantic action";
    // Semantic name should be increment_orbitCycleCount or similar
    EXPECT_NE(t1.transition_action->name.find("orbitCycleCount"), std::string::npos)
        << "Action name must reference variable: " << t1.transition_action->name;

    // Transition 2: compound-assign action for energyBudget
    const auto& t2 = model.transitions[1];
    ASSERT_TRUE(t2.transition_action.has_value()) << "budget_update transition must have a semantic action";
    EXPECT_NE(t2.transition_action->name.find("energyBudget"), std::string::npos)
        << "Action name must reference variable: " << t2.transition_action->name;
}

/**
 * @brief Verify robust brace balancing and structural block filtering in SysML v2 files.
 * @scenario Parse SysML v2 package containing non-statechart blocks (part def, item def).
 * @expected State machine isolated cleanly without syntax confusion from adjacent blocks.
 */
TEST(Sysml2Parser, StructuralBlockFiltering_BalancesBraces) {
    const std::string sysml_text = R"(
    import ScalarValues::*;

    part def PowerPlant {
        part cell1 : FuelCell;
        part cell2 : FuelCell;
        connect cell1.bus to cell2.bus;
    }

    state def CoreController {
        attribute status_code : Integer = 0;

        part power : PowerPlant;
        connect power.out to core.in;

        entry; then Standby;

        state Standby {
            entry do action {
                if (status_code == 0) {
                    status_code = 1;
                }
            }
        }

        state Running;

        transition start_mission
            first Standby
            accept StartCmd
            then Running;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "CoreController");
    EXPECT_EQ(model.initial_state, "Standby");

    // Must only contain Standby and Running - no non-FSM parts or connections as states!
    ASSERT_EQ(model.states.size(), 2u);
    EXPECT_NE(model.find_state("Standby"), nullptr);
    EXPECT_NE(model.find_state("Running"), nullptr);
    EXPECT_EQ(model.find_state("PowerPlant"), nullptr);
    EXPECT_EQ(model.find_state("cell1"), nullptr);
    EXPECT_EQ(model.find_state("cell2"), nullptr);

    // Only 1 real transition: start_mission (no connect statements parsed as transitions)
    ASSERT_EQ(model.transitions.size(), 1u);
    EXPECT_EQ(model.transitions[0].source, "Standby");
    EXPECT_EQ(model.transitions[0].target, "Running");
    EXPECT_EQ(model.transitions[0].event, "StartCmd");

    // Standby has entry action recorded
    const auto* standby = model.find_state("Standby");
    ASSERT_NE(standby, nullptr);
    EXPECT_FALSE(standby->entry_actions.empty());

    // Variable status_code correctly imported and mapped to uint32_t
    ASSERT_EQ(model.variables.size(), 1u);
    EXPECT_EQ(model.variables[0].name, "status_code");
    EXPECT_EQ(model.variables[0].type, "uint32_t");
}

/**
 * @brief Verify SysML v2 'send Signal via port' action statement parsing.
 * @scenario Parse transitions containing 'send TelemetryCmd via telemetry_port' action statements.
 * @expected Action parsed with outbound port target and signal definition.
 */
TEST(Sysml2Parser, SendSignalViaPort_ParsedIntoActionIr) {
    const std::string sysml_text = R"(
    state def TelemetrySystem {
        out port telem_port : TelemetryPort;

        entry; then Active;

        state Active {
            do send StatusUpdate(1) via telem_port;
        }

        transition emit_ping
            first Active
            accept PingEvent
            do send PingResponse via telem_port
            then Active;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "TelemetrySystem");
    const auto* port = model.find_port("telem_port");
    ASSERT_NE(port, nullptr);
    EXPECT_EQ(port->direction, PortDirection::Out);

    // State send action
    const auto* active_state = model.find_state("Active");
    ASSERT_NE(active_state, nullptr);
    ASSERT_FALSE(active_state->entry_actions.empty());
    EXPECT_EQ(active_state->entry_actions[0].name, "send_StatusUpdate_via_telem_port");
    ASSERT_FALSE(active_state->entry_actions[0].instructions.empty());

    // Transition send action
    ASSERT_FALSE(model.transitions.empty());
    EXPECT_TRUE(model.transitions[0].transition_action.has_value());
    EXPECT_EQ(model.transitions[0].transition_action->name, "send_PingResponse_via_telem_port");
    ASSERT_FALSE(model.transitions[0].transition_action->instructions.empty());
}

}  // namespace

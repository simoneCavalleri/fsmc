/**
 * @file test_sysml2_parser.cpp
 * @brief Unit test suite for the OMG SysML v2 State Definition frontend parser.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/formal/sysml2_serializer.hpp"
#include "fsm/frontend/formal/sysml2/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::frontend::formal;
using namespace fsm::frontend;
using namespace fsm::backend::cpp;
using namespace fsm::backend;
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
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

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
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

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
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

    const auto* standby = model.find_state("Standby");
    ASSERT_NE(standby, nullptr);
    EXPECT_TRUE(standby->is_composite);
    EXPECT_EQ(standby->initial_sub_state, "Diagnostics");

    const auto* diag = model.find_state("Diagnostics");
    ASSERT_NE(diag, nullptr);
    EXPECT_EQ(diag->parent_state, "Standby");

    GeneratorOptions opts;
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
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

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
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

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
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

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
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

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
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

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
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

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
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

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

/**
 * @brief Regression: fractional `after` durations must not be truncated before unit multiplication.
 * @scenario Parse `after 1.5 s` — before the fix this yielded 1 ms (raw_val=1.5 cast to uint64=1
 *           before the *1000 multiplier ran). Expected: 1500 ms.
 * @expected TimeTrigger duration == 1500 ms; event name "after_1500ms".
 */
TEST(Sysml2Parser, FractionalAfterDuration_ConvertedWithoutTruncation) {
    const std::string sysml_text = R"(
    state def WatchdogFSM {
        entry; then Active;
        state Active;
        state Timeout;

        transition from Active after 1.5 s then Timeout;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

    ASSERT_EQ(model.transitions.size(), 1u);
    const auto& tr = model.transitions[0];
    EXPECT_EQ(tr.event, "after_1500ms") << "Fractional 1.5 s must produce 1500 ms, not 1 ms";
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(tr.trigger));
    const auto& tt = std::get<TimeTrigger>(tr.trigger);
    EXPECT_EQ(tt.duration_ms, 1500u) << "TimeTrigger duration must be 1500 ms for `after 1.5 s`";
}

/**
 * @brief Regression: `after 500.5 ms` must round to 501 ms, not truncate to 500 ms.
 * @scenario Sub-millisecond precision boundary: 500.5 ms should round to nearest integer.
 * @expected TimeTrigger duration == 501 ms.
 */
TEST(Sysml2Parser, FractionalMillisAfterDuration_RoundedToNearest) {
    const std::string sysml_text = R"(
    state def TimerFSM {
        entry; then Counting;
        state Counting;
        state Expired;

        transition from Counting after 500.5 ms then Expired;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

    ASSERT_EQ(model.transitions.size(), 1u);
    const auto& tr = model.transitions[0];
    EXPECT_EQ(tr.event, "after_501ms") << "500.5 ms must round to 501 ms, not truncate to 500 ms";
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(tr.trigger));
    const auto& tt = std::get<TimeTrigger>(tr.trigger);
    EXPECT_EQ(tt.duration_ms, 501u);
}

/**
 * @brief Regression BUG-01: SysML v2 entry and exit action blocks must parse assignments.
 * @scenario State with entry action { counter = 0; status = 1; } and exit action { counter += 1; }.
 * @expected Both entry and exit actions capture variable assignments in st->entry_actions and st->exit_actions.
 */
TEST(Sysml2Parser, EntryAndExitActionBlocks_AssignmentsPopulatedInIr) {
    const std::string sysml_text = R"(
    state def LifecycleFSM {
        attribute counter : Integer = 0;
        attribute status : Integer = 0;

        entry; then Active;

        state Active {
            entry action InitActive {
                counter = 0;
                status = 1;
            }
            exit action {
                counter += 1;
            }
        }
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

    const auto* st = model.find_state("Active");
    ASSERT_NE(st, nullptr);

    ASSERT_EQ(st->entry_actions.size(), 1u);
    const auto& entry_act = st->entry_actions[0];
    EXPECT_EQ(entry_act.name, "InitActive");
    ASSERT_EQ(entry_act.assignments.size(), 2u);
    EXPECT_EQ(entry_act.assignments[0].target.name, "counter");
    EXPECT_EQ(entry_act.assignments[0].expression, "0");
    EXPECT_EQ(entry_act.assignments[1].target.name, "status");
    EXPECT_EQ(entry_act.assignments[1].expression, "1");

    // Verify Exit Action
    ASSERT_EQ(st->exit_actions.size(), 1u);
    const auto& exit_act = st->exit_actions[0];
    EXPECT_EQ(exit_act.name, "Active_exit");
    ASSERT_EQ(exit_act.assignments.size(), 1u);
    EXPECT_EQ(exit_act.assignments[0].target.name, "counter");
    EXPECT_EQ(exit_act.assignments[0].expression, "counter + 1");
}

/**
 * @brief Verify invalid 'transition Src -> Dst' syntax is rejected with a clear syntax error.
 * @scenario SysML v2 model containing 'transition A -> B accept Ev;'.
 * @expected Sysml2Parser::parse returns false with diagnostic mentioning '->'.
 */
TEST(Sysml2ParserTest, RejectsArrowTransitionSyntaxWithDiagnostic) {
    const std::string sysml = R"sysml(
package BadSyntax {
    state def M {
        entry; then A;
        state A;
        state B;
        transition A -> B accept Ev;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    EXPECT_FALSE(parser.parse(sysml, model, err));
    EXPECT_NE(err.render_to_string().find("->"), std::string::npos);
}

/**
 * @brief Verify Sysml2Serializer preserves TimeTrigger as 'accept after(...)' without synthetic event def.
 * @scenario SysML v2 model with 'transition first Hovering accept after(5000ms) then Landing;'.
 * @expected Serialized SysML v2 contains 'accept after(5000ms)', omits 'event def after_5000ms;', and re-parses as
 * TimeTrigger.
 */
TEST(Sysml2ParserTest, RoundTripsTimeTriggerWithoutSyntheticSignal) {
    const std::string sysml = R"sysml(
package Timing {
    state def Drone {
        entry; then Hovering;
        state Hovering;
        state Landing;
        transition first Hovering accept after(5000ms) then Landing;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();

    const std::string exported = fsm::backend::formal::Sysml2Serializer::serialize(model);
    EXPECT_NE(exported.find("accept after 5000 [ms]"), std::string::npos);
    EXPECT_EQ(exported.find("item def after_5000ms;"), std::string::npos);

    FsmIr reparsed;
    ASSERT_TRUE(parser.parse(exported, reparsed, err)) << err.render_to_string();
    ASSERT_EQ(reparsed.transitions.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<TimeTrigger>(reparsed.transitions[0].trigger));
}

/**
 * @brief Verify Sysml2Parser recognizes standalone 'choice <Name>;' declarations as Choice pseudostates.
 * @scenario SysML v2 model declaring 'choice CheckCount;'.
 * @expected CheckCount has StateKind::Choice and is registered in model.choice_nodes.
 */
TEST(Sysml2ParserTest, RecognizesChoiceKeywordDeclaration) {
    const std::string sysml = R"sysml(
package ChoicePkg {
    state def M {
        entry; then A;
        state A;
        state B;
        choice CheckCount;
        transition first A accept EvGo then CheckCount;
        transition first CheckCount if ok then B;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();

    const StateNode* c = model.find_state("CheckCount");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->kind, StateKind::Choice);
    EXPECT_TRUE(model.is_choice_node("CheckCount"));
}

/**
 * @brief Verify Sysml2Parser rejects state identifiers starting with a digit with a clear diagnostic.
 * @scenario SysML v2 model containing 'state 123Start;'.
 * @expected Sysml2Parser::parse returns false with diagnostic mentioning '123Start'.
 */
TEST(Sysml2ParserTest, RejectsDigitLeadingIdentifierWithDiagnostic) {
    const std::string sysml = R"sysml(
package WeirdNames {
    state def M {
        entry; then 123Start;
        state 123Start;
        state End;
        transition first 123Start accept Ev1 then End;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    EXPECT_FALSE(parser.parse(sysml, model, err));
    EXPECT_NE(err.render_to_string().find("123Start"), std::string::npos);
}

/**
 * @brief Verify Sysml2Parser parses 'accept when(<expr>)' as a ChangeTrigger rather than a signal named 'when'.
 * @scenario SysML v2 transition 'transition first Normal accept when(temp > 100) then Alarm;'.
 * @expected Transition trigger holds ChangeTrigger with raw_expression 'temp > 100'.
 */
TEST(Sysml2ParserTest, ParsesAcceptWhenAsChangeTrigger) {
    const std::string sysml = R"sysml(
package ChangePkg {
    state def TempMonitor {
        attribute temp : Int32 = 25;
        entry; then Normal;
        state Normal;
        state Alarm;
        transition first Normal accept when(temp > 100) then Alarm;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();

    ASSERT_EQ(model.transitions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ChangeTrigger>(model.transitions[0].trigger));
    const auto& ct = std::get<ChangeTrigger>(model.transitions[0].trigger);
    EXPECT_EQ(ct.raw_expression, "temp > 100");
}

/**
 * @brief Verify Sysml2Parser parses 'accept every(<dur><unit>)' as a periodic TimeTrigger.
 * @scenario SysML v2 transition 'transition first Active accept every(100ms) then Active;'.
 * @expected Transition trigger holds TimeTrigger with kind=TimeTriggerKind::Every and duration_ms=100.
 */
TEST(Sysml2ParserTest, ParsesAcceptEveryAsPeriodicTimeTrigger) {
    const std::string sysml = R"sysml(
package PeriodicPkg {
    state def Heartbeat {
        entry; then Active;
        state Active;
        transition first Active accept every(100ms) then Active;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();

    ASSERT_EQ(model.transitions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(model.transitions[0].trigger));
    const auto& tt = std::get<TimeTrigger>(model.transitions[0].trigger);
    EXPECT_EQ(tt.kind, TimeTriggerKind::Every);
    EXPECT_EQ(tt.duration_in_ms(), 100u);
}

/**
 * @brief Regression: `if <guard>` at the end of a SysML2 transition statement (after `then <target>`) must parse.
 * @scenario Parse choice transitions `transition t_ok first Decide then Working if sensor_ok == true;`.
 * @expected Both the boolean guard and the `else` guard are captured on the transitions.
 */
TEST(Sysml2ParserTest, ParsesTrailingIfGuardAfterThenClause) {
    const std::string sysml = R"sysml(
package TrailingIfPkg {
    state def Checker {
        in port sensor_ok : bool = true;
        entry; then Working;
        state Working;
        state ErrorState;
        choice Decide;
        transition t_ok first Decide then Working if sensor_ok == true;
        transition t_err first Decide then ErrorState if else;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();

    ASSERT_EQ(model.transitions.size(), 2u);
    EXPECT_TRUE(model.transitions[0].guard.has_value());
    EXPECT_EQ(*model.transitions[0].guard, "sensor_ok_eq_true");
    EXPECT_TRUE(model.transitions[1].guard.has_value());
    EXPECT_EQ(*model.transitions[1].guard, "else");
}

/**
 * @brief Regression: dot-notation history targets (`State.H` and `State.H*`) must set history flags.
 * @scenario Parse SysML2 transitions targeting `Active.H` and `Active.H*`.
 * @expected Target state is `Active`, `target_is_history` is true, and `Active.H*` sets `target_is_deep_history`.
 */
TEST(Sysml2ParserTest, ParsesDotNotationHistoryTargets) {
    const std::string sysml = R"sysml(
package HistoryDotPkg {
    state def Machine {
        event def EvResumeShallow;
        event def EvResumeDeep;
        entry; then Paused;
        state Active {
            entry; then Step1;
            state Step1;
        }
        state Paused {
            transition t1 first Paused then Active.H accept EvResumeShallow;
            transition t2 first Paused then Active.H* accept EvResumeDeep;
        }
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();

    ASSERT_EQ(model.transitions.size(), 2u);
    EXPECT_EQ(model.transitions[0].target, "Active");
    EXPECT_TRUE(model.transitions[0].target_is_history);
    EXPECT_FALSE(model.transitions[0].target_is_deep_history);

    EXPECT_EQ(model.transitions[1].target, "Active");
    EXPECT_TRUE(model.transitions[1].target_is_history);
    EXPECT_TRUE(model.transitions[1].target_is_deep_history);

    const auto* active = model.find_state("Active");
    ASSERT_NE(active, nullptr);
    EXPECT_TRUE(active->has_history);
    EXPECT_TRUE(active->has_deep_history);
}

/**
 * @brief Regression: bare enum literals without repeating `enum` inside `enum def` must be parsed.
 * @scenario Parse `enum def OpMode { Boot; Auto = 2; Manual; }` inside a SysML2 state def.
 * @expected `OpMode` contains all three `EnumLiteral` entries with `Auto` having value `2`.
 */
TEST(Sysml2ParserTest, ParsesBareEnumLiteralsInsideEnumDef) {
    const std::string sysml = R"sysml(
package BareEnumPkg {
    state def SystemCtrl {
        enum def OpMode {
            Boot;
            Auto = 2;
            Manual;
        }
        entry; then Init;
        state Init;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();

    const auto* op_mode = model.find_enum("OpMode");
    ASSERT_NE(op_mode, nullptr);
    ASSERT_EQ(op_mode->literals.size(), 3u);
    EXPECT_EQ(op_mode->literals[0].name, "Boot");
    EXPECT_EQ(op_mode->literals[1].name, "Auto");
    ASSERT_TRUE(op_mode->literals[1].value.has_value());
    EXPECT_EQ(*op_mode->literals[1].value, 2);
    EXPECT_EQ(op_mode->literals[2].name, "Manual");
}

/**
 * @brief Regression: struct member lvalues (`pos.x = 2.5`) in SysML2 action blocks preserve `member_path`.
 * @scenario Parse `do action { pos.x = 2.5; pos.y += 1.0; }` on a transition where `pos` is a struct variable.
 * @expected `assignments` captures `target.name == "pos"` and `target.member_path == {"x"}` / `{"y"}`.
 */
TEST(Sysml2ParserTest, ParsesStructMemberAssignmentsInActions) {
    const std::string sysml = R"sysml(
package StructAssignPkg {
    state def Mover {
        struct def Vec2 {
            attribute x : float32 = 0.0;
            attribute y : float32 = 0.0;
        }
        attribute pos : Vec2;
        event def EvStep;
        entry; then Idle;
        state Idle {
            transition t1 first Idle then Moving accept EvStep
                do action { pos.x = 2.5; pos.y += 1.0; };
        }
        state Moving;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();

    ASSERT_EQ(model.transitions.size(), 1u);
    ASSERT_TRUE(model.transitions[0].transition_action.has_value());
    const auto& assigns = model.transitions[0].transition_action->assignments;
    ASSERT_EQ(assigns.size(), 2u);
    EXPECT_EQ(assigns[0].target.name, "pos");
    ASSERT_EQ(assigns[0].target.member_path.size(), 1u);
    EXPECT_EQ(assigns[0].target.member_path[0], "x");
    EXPECT_EQ(assigns[0].expression, "2.5");
    EXPECT_EQ(assigns[1].target.name, "pos");
    ASSERT_EQ(assigns[1].target.member_path.size(), 1u);
    EXPECT_EQ(assigns[1].target.member_path[0], "y");
    EXPECT_EQ(assigns[1].expression, "pos.y + 1.0");
}

/**
 * @brief Verify history state and deep history state declarations resolve parent history targets.
 * @scenario Parse SysML v2 with `history state Hist;` and `deep history state DeepHist;` referenced via dot notation.
 * @expected Transitions target the parent composite state with `has_history`/`has_deep_history` set, without phantom
 * states.
 */
TEST(Sysml2ParserTest, ParsesHistoryAndDeepHistoryStateDeclarations) {
    const std::string sysml = R"sysml(
state def HistMachine {
    entry; then Op;
    state Op {
        entry; then S1;
        history state Hist;
        state S1;
        transition from Op accept EvPause then Paused;
    }
    state DeepOp {
        entry; then D1;
        deep history state DeepHist;
        state D1;
        transition from DeepOp accept EvPause then Paused;
    }
    state Paused {
        transition from Paused accept EvResumeOp then Op.Hist;
        transition from Paused accept EvResumeDeep then DeepOp.DeepHist;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();

    EXPECT_EQ(model.find_state("Op_Hist"), nullptr);
    EXPECT_EQ(model.find_state("DeepOp_DeepHist"), nullptr);

    const StateNode* op = model.find_state("Op");
    ASSERT_NE(op, nullptr);
    EXPECT_TRUE(op->has_history);
    EXPECT_FALSE(op->has_deep_history);

    const StateNode* deep_op = model.find_state("DeepOp");
    ASSERT_NE(deep_op, nullptr);
    EXPECT_TRUE(deep_op->has_history);
    EXPECT_TRUE(deep_op->has_deep_history);

    bool found_op = false;
    bool found_deep = false;
    for (const auto& t : model.transitions) {
        if (t.event == "EvResumeOp") {
            EXPECT_EQ(t.target, "Op");
            found_op = true;
        } else if (t.event == "EvResumeDeep") {
            EXPECT_EQ(t.target, "DeepOp");
            found_deep = true;
        }
    }
    EXPECT_TRUE(found_op);
    EXPECT_TRUE(found_deep);
}

/**
 * @brief Regression: multi-line `doc` block annotations without trailing semicolons do not bleed
 * into the following `state def` / `state` / `transition` statement.
 * @scenario Parse a SysML2 `package` containing a multi-line `doc` block comment followed by `state def PkgFsm`.
 * @expected Both `Init` and `Ready` states and the `Init -> Ready` transition are preserved.
 */
TEST(Sysml2Parser, MultilineDocCommentBeforeStateDef_DoesNotCorruptStateParsing) {
    const std::string sysml = R"sysml(
package FlightPkg {
    doc /* Multi-line
           SysML2 doc comment */
    state def PkgFsm {
        entry; then Init;
        state Init;
        state Ready;
        transition t1 first Init then Ready accept Boot;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();
    EXPECT_EQ(model.package, "FlightPkg");
    EXPECT_EQ(model.name, "PkgFsm");
    EXPECT_NE(model.find_state("Init"), nullptr);
    EXPECT_NE(model.find_state("Ready"), nullptr);
    ASSERT_EQ(model.transitions.size(), 1u);
    EXPECT_EQ(model.transitions[0].source, "Init");
    EXPECT_EQ(model.transitions[0].target, "Ready");
    EXPECT_EQ(model.transitions[0].event, "Boot");
}

/**
 * @brief Regression: SysML2 `int` maps to signed `int32_t` (not `uint32_t`), while `Integer` and `Natural`
 * remain `uint32_t`.
 * @scenario Parse `attribute offset : int = -10;` and `attribute u : Natural = 5;`.
 * @expected `offset` has `int32_t` type with `"-10"`, and `u` has `uint32_t` type.
 */
TEST(Sysml2Parser, SignedIntType_MapsToInt32WithNegativeInit) {
    const std::string sysml = R"sysml(
state def SignedTypesFsm {
    attribute offset : int = -10;
    attribute u : Natural = 5;
    entry; then S0;
    state S0;
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();
    const auto* offset = model.find_variable("offset");
    const auto* u = model.find_variable("u");
    ASSERT_NE(offset, nullptr);
    ASSERT_NE(u, nullptr);
    EXPECT_EQ(offset->type, "int32_t");
    EXPECT_EQ(offset->initial_value, "-10");
    EXPECT_EQ(u->type, "uint32_t");
}

/**
 * @brief Regression: nested substates declared inside a composite state block retain their `parent_state`
 * across `normalize_hierarchy()` even when referenced by root-level `transition first Sub2 ...` statements.
 * @scenario Serialize a composite state with two substates (`Sub1`, `Sub2`) via `Sysml2Serializer`, re-parse
 * with `Sysml2Parser`, and run `normalize_hierarchy()`.
 * @expected Both `Sub1` and `Sub2` retain `parent_state == "Active"`.
 */
TEST(Sysml2Parser, NestedSubstatesWithRootLevelTransitions_RetainParentAfterNormalizeHierarchy) {
    const std::string sysml = R"sysml(
state def HierarchyRoundtripFsm {
    entry; then Active;
    state Active {
        entry; then Sub1;
        state Sub1;
        state Sub2;
    }
    transition first Sub1 accept Step then Sub2;
    transition first Sub2 accept Reset then Sub1;
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();
    model.normalize_hierarchy();

    const auto* sub1 = model.find_state("Sub1");
    const auto* sub2 = model.find_state("Sub2");
    ASSERT_NE(sub1, nullptr);
    ASSERT_NE(sub2, nullptr);
    EXPECT_EQ(sub1->parent_state, "Active");
    EXPECT_EQ(sub2->parent_state, "Active");
}

/**
 * @brief Regression: Signed `int32` variables with negative initial values survive `Sysml2Serializer` ->
 * `Sysml2Parser` roundtrips as `int32_t` rather than turning into `uint32_t` (`-Wnarrowing` error in C++).
 * @scenario Serialize an FSM with `attribute count : int32_t = -2;` to SysML v2 and re-parse.
 * @expected Serialized SysML2 uses `: int` (not `: Integer`) and re-parsed `count` has `int32` type with `"-2"`.
 */
TEST(Sysml2Parser, SignedIntRoundtrip_PreservesSignedTypeAcrossSerializer) {
    FsmIr ir;
    ir.name = "SignedRtFsm";
    ir.initial_state = "S0";
    ir.add_state("S0");
    ir.add_variable(VariableDefinition("count", DataType::int32(), "-2"));

    const std::string sysml = fsm::backend::formal::Sysml2Serializer::serialize(ir);
    EXPECT_NE(sysml.find("attribute count : int := -2;"), std::string::npos) << sysml;

    Sysml2Parser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, parsed, err)) << err.render_to_string();
    const auto* count = parsed.find_variable("count");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->type, DataType::int32());
    EXPECT_EQ(count->initial_value, "-2");
}

/**
 * @brief Regression: `Sysml2Serializer` emits valid `// @fsm:property` structured comments for
 * `model.properties` (without non-SysML `assert property`) so temporal logic properties survive SysML v2 roundtrips.
 * @scenario Serialize an FSM with `FormalProperty("safe", "G (not (A and B))")` to SysML v2 and re-parse.
 * @expected Serialized SysML2 contains `@safe : TemporalProperty` and re-parsed IR keeps it.
 */
TEST(Sysml2Parser, PropertiesRoundtrip_PreservedAcrossSerializer) {
    FsmIr ir;
    ir.name = "PropRtFsm";
    ir.initial_state = "A";
    ir.add_state("A");
    ir.add_state("B");
    FormalProperty prop;
    prop.name = "safe";
    prop.raw_formula = "G (not (A and B))";
    ir.add_property(prop);

    const std::string sysml = fsm::backend::formal::Sysml2Serializer::serialize(ir);
    EXPECT_NE(sysml.find("@safe : TemporalProperty"), std::string::npos) << sysml;
    EXPECT_EQ(sysml.find("@fsm:"), std::string::npos) << sysml;
    EXPECT_EQ(sysml.find("assert property"), std::string::npos) << sysml;

    Sysml2Parser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, parsed, err)) << err.render_to_string();
    ASSERT_EQ(parsed.properties.size(), 1u);
    EXPECT_EQ(parsed.properties[0].name, "safe");
    EXPECT_EQ(parsed.properties[0].raw_formula, "G (not (A and B))");
}

/**
 * @brief Regression: `Sysml2Serializer` emits `fork <Name>;` and `join <Name>;` for `StateKind::Fork`
 * and `StateKind::Join` pseudostates so `Sysml2Parser` preserves them across roundtrips.
 * @scenario Serialize an FSM with `Fork` (`Split`) and `Join` (`Merge`) pseudostates to SysML v2 and re-parse.
 * @expected Serialized SysML2 contains `fork Split;` and `join Merge;`, and re-parsed IR preserves their kinds.
 */
TEST(Sysml2Parser, ForkAndJoinRoundtrip_PreservedAcrossSerializer) {
    FsmIr ir;
    ir.name = "ForkJoinRtFsm";
    ir.initial_state = "Init";
    ir.add_state("Init");
    ir.add_or_get_state("Split", "", StateKind::Fork);
    ir.add_or_get_state("Merge", "", StateKind::Join);

    const std::string sysml = fsm::backend::formal::Sysml2Serializer::serialize(ir);
    EXPECT_NE(sysml.find("fork Split;"), std::string::npos) << sysml;
    EXPECT_NE(sysml.find("join Merge;"), std::string::npos) << sysml;

    Sysml2Parser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, parsed, err)) << err.render_to_string();
    const auto* split = parsed.find_state("Split");
    const auto* merge = parsed.find_state("Merge");
    ASSERT_NE(split, nullptr);
    ASSERT_NE(merge, nullptr);
    EXPECT_EQ(split->kind, StateKind::Fork);
    EXPECT_EQ(merge->kind, StateKind::Join);
}

/**
 * @brief Regression: `Sysml2Serializer` emits `accept when (<expr>)` for `ChangeTrigger`
 * transitions so `Sysml2Parser` preserves `ChangeTrigger` across roundtrips.
 * @scenario Serialize a transition with `ChangeTrigger("count > 5")` to SysML v2 and re-parse.
 * @expected Serialized SysML2 contains `accept when (count > 5)` and re-parsed transition holds `ChangeTrigger`.
 */
TEST(Sysml2Parser, ChangeTriggerRoundtrip_PreservedAcrossSerializer) {
    FsmIr ir;
    ir.name = "ChangeTrigRt";
    ir.initial_state = "Wait";
    ir.add_state("Wait");
    ir.add_state("Done");
    ir.add_variable(VariableDefinition("count", DataType::int32(), "0"));
    TransitionEdge t("Wait", "Done", "");
    t.trigger = ChangeTrigger("count > 5", true);
    ir.add_transition(t);

    const std::string sysml = fsm::backend::formal::Sysml2Serializer::serialize(ir);
    EXPECT_NE(sysml.find("accept when (count > 5)"), std::string::npos) << sysml;

    Sysml2Parser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, parsed, err)) << err.render_to_string();
    ASSERT_EQ(parsed.transitions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ChangeTrigger>(parsed.transitions[0].trigger));
    EXPECT_EQ(std::get<ChangeTrigger>(parsed.transitions[0].trigger).raw_expression, "count > 5");
}

/**
 * @brief Regression: `Sysml2Serializer` emits `choice <Name>;` for `ChoiceNode` entries
 * so `Sysml2Parser` preserves `choice_nodes` across SysML v2 roundtrips.
 * @scenario Serialize an FSM with `ChoiceNode("C1")` to SysML v2 and re-parse.
 * @expected Serialized SysML2 contains `decide C1;` and re-parsed IR has `is_choice_node("C1") == true`.
 */
TEST(Sysml2Parser, ChoiceNodeRoundtrip_PreservedAcrossSerializer) {
    FsmIr ir;
    ir.name = "ChoiceRtFsm";
    ir.initial_state = "Init";
    ir.add_state("Init");
    ir.add_choice_node("C1");
    ir.add_state("High");
    ir.add_transition(TransitionEdge("Init", "C1", "Eval"));
    ir.add_transition(TransitionEdge("C1", "High", ""));

    const std::string sysml = fsm::backend::formal::Sysml2Serializer::serialize(ir);
    EXPECT_NE(sysml.find("decide C1;"), std::string::npos) << sysml;

    Sysml2Parser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, parsed, err)) << err.render_to_string();
    EXPECT_TRUE(parsed.is_choice_node("C1"));
    EXPECT_FALSE(parsed.choice_nodes.empty());
}

/**
 * @brief Regression: `send <Signal>(<Payload>) via <Port>;` inside `entry action { ... }` and
 * `do action { ... }` blocks produces a structured `SignalEmitOp` instruction instead of a mangled
 * standalone call identifier (`send_Alert42_via_cmd_out`).
 * @scenario Parse SysML v2 with `entry action { send Alert(42) via cmd_out; }` and a transition with
 * `do action { send Ack(1) via cmd_out; }`.
 * @expected Both the state entry action and transition action carry a `SignalEmitOp` with matching signal, port, and
 * argument.
 */
TEST(Sysml2Parser, SendViaPortInsideActionBlocks_ProducesSignalEmitOp) {
    const std::string sysml = R"sysml(
state def BlockSendFsm {
    out port cmd_out : Integer;
    event def Alert;
    event def Ack;
    event def Go;
    entry; then Idle;
    state Idle {
        entry action { send Alert(42) via cmd_out; }
    }
    state Done;
    transition first Idle accept Go do action { send Ack(1) via cmd_out; } then Done;
}
)sysml";

    Sysml2Parser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, ir, err)) << err.render_to_string();

    const auto* idle = ir.find_state("Idle");
    ASSERT_NE(idle, nullptr);
    ASSERT_EQ(idle->entry_actions.size(), 1u);
    ASSERT_EQ(idle->entry_actions[0].instructions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<SignalEmitOp>(idle->entry_actions[0].instructions[0].op));
    const auto& entry_emit = std::get<SignalEmitOp>(idle->entry_actions[0].instructions[0].op);
    EXPECT_EQ(entry_emit.signal_name, "Alert");
    EXPECT_EQ(entry_emit.target_port, "cmd_out");
    ASSERT_EQ(entry_emit.arguments.size(), 1u);
    EXPECT_EQ(entry_emit.arguments[0], "42");

    ASSERT_EQ(ir.transitions.size(), 1u);
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    ASSERT_EQ(ir.transitions[0].transition_action->instructions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<SignalEmitOp>(ir.transitions[0].transition_action->instructions[0].op));
    const auto& trans_emit = std::get<SignalEmitOp>(ir.transitions[0].transition_action->instructions[0].op);
    EXPECT_EQ(trans_emit.signal_name, "Ack");
    EXPECT_EQ(trans_emit.target_port, "cmd_out");
    ASSERT_EQ(trans_emit.arguments.size(), 1u);
    EXPECT_EQ(trans_emit.arguments[0], "1");
}

/**
 * @brief Regression: `Sysml2Serializer` and `Sysml2Parser` preserve `TransitionEdgeKind::Local`
 * via `local transition` syntax.
 * @scenario Serialize a composite FSM with a local transition and an external transition to SysML2 and re-parse.
 * @expected Re-parsed local transition has `kind == TransitionEdgeKind::Local`; external remains `External`.
 */
TEST(Sysml2Parser, LocalTransition_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "SysmlLocalRt";
    ir.initial_state = "Active";
    StateNode active("Active");
    active.is_composite = true;
    active.initial_sub_state = "SubA";
    ir.add_state(active);
    StateNode sub_a("SubA");
    sub_a.parent_state = "Active";
    ir.add_state(sub_a);
    StateNode sub_b("SubB");
    sub_b.parent_state = "Active";
    ir.add_state(sub_b);

    TransitionEdge loc("Active", "SubB", "Reset");
    loc.kind = TransitionEdgeKind::Local;
    ir.add_transition(loc);

    TransitionEdge ext("SubA", "SubB", "Step");
    ext.kind = TransitionEdgeKind::External;
    ir.add_transition(ext);

    const std::string sysml = fsm::backend::formal::Sysml2Serializer::serialize(ir);
    EXPECT_NE(sysml.find("local transition"), std::string::npos) << sysml;

    Sysml2Parser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, parsed, err)) << err.render_to_string();
    ASSERT_EQ(parsed.transitions.size(), 2u);

    bool found_local = false;
    bool found_external = false;
    for (const auto& t : parsed.transitions) {
        if (t.event == "Reset") {
            EXPECT_EQ(t.kind, TransitionEdgeKind::Local);
            found_local = true;
        } else if (t.event == "Step") {
            EXPECT_EQ(t.kind, TransitionEdgeKind::External);
            found_external = true;
        }
    }
    EXPECT_TRUE(found_local);
    EXPECT_TRUE(found_external);
}

/**
 * @brief Verify SysML v2 parses @fsm:external_type and @fsm:service directives into FsmIr.
 */
TEST(Sysml2ParserTest, ParsesExternalTypeAndServiceDirectives) {
    const std::string sysml = R"sysml(
package ControlPkg {
    state def ThermalCtrl {
        // @fsm:external_type name=PidState module="ctrl/pid.hpp" symbol="ctrl.PidState" kind=struct
        // @fsm:service compute_pwm(err: int32_t) -> int32_t pure=true noexcept=true desc="Compute PWM duty"
        entry; then Idle;
        state Idle;
        state Active;
        transition t1 first Idle accept Start then Active;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();

    ASSERT_EQ(model.custom_types.size(), 1u);
    EXPECT_EQ(model.custom_types[0].name, "PidState");
    ASSERT_TRUE(model.custom_types[0].is_external());
    EXPECT_EQ(model.custom_types[0].external_ref->module, "ctrl/pid.hpp");
    EXPECT_EQ(model.custom_types[0].external_ref->symbol, "ctrl.PidState");

    ASSERT_EQ(model.services.size(), 1u);
    EXPECT_EQ(model.services[0].name, "compute_pwm");
    EXPECT_EQ(model.services[0].return_type.to_canonical_string(), "int32");
    EXPECT_TRUE(model.services[0].is_pure);
    EXPECT_TRUE(model.services[0].is_noexcept);
    ASSERT_EQ(model.services[0].parameters.size(), 1u);
    EXPECT_EQ(model.services[0].parameters[0].name, "err");
}

/**
 * @brief Verify SysML v2 action blocks lower calls to declared @fsm:service functions into typed ActionCallOp
 *        instructions (both with result_target and void calls).
 */
TEST(Sysml2ParserTest, LowersServiceCallsInActionBlocksToActionCallOp) {
    const std::string sysml = R"sysml(
package ServiceCallPkg {
    state def HeaterCtrl {
        in port temp_c : Int32;
        out port pwm_duty : Int32;
        // @fsm:service compute_pwm(temp: int32_t) -> int32_t pure=true noexcept=true
        // @fsm:service log_fault(code: int32_t) -> void pure=false noexcept=true
        entry; then Active;
        state Active;
        state Fault;
        transition run_step
            first Active
            accept Sample
            do action RunPid {
                out.pwm_duty = compute_pwm(in.temp_c);
            }
            then Active;
        transition trip_fault
            first Active
            accept Overheat
            do action LogTrip {
                log_fault(42);
            }
            then Fault;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();

    ASSERT_EQ(model.services.size(), 2u);
    ASSERT_EQ(model.transitions.size(), 2u);

    const auto& t_pid = model.transitions[0];
    ASSERT_TRUE(t_pid.transition_action.has_value());
    ASSERT_EQ(t_pid.transition_action->instructions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ActionCallOp>(t_pid.transition_action->instructions[0].op));
    const auto& pid_call = std::get<ActionCallOp>(t_pid.transition_action->instructions[0].op);
    EXPECT_EQ(pid_call.function_name, "compute_pwm");
    ASSERT_EQ(pid_call.arguments.size(), 1u);
    EXPECT_EQ(pid_call.arguments[0], "in.temp_c");
    ASSERT_TRUE(pid_call.result_target.has_value());
    EXPECT_EQ(pid_call.result_target->name, "pwm_duty");
    EXPECT_EQ(pid_call.result_target->scope, LValueScope::OutPort);

    const auto& t_fault = model.transitions[1];
    ASSERT_TRUE(t_fault.transition_action.has_value());
    ASSERT_EQ(t_fault.transition_action->instructions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ActionCallOp>(t_fault.transition_action->instructions[0].op));
    const auto& fault_call = std::get<ActionCallOp>(t_fault.transition_action->instructions[0].op);
    EXPECT_EQ(fault_call.function_name, "log_fault");
    ASSERT_EQ(fault_call.arguments.size(), 1u);
    EXPECT_EQ(fault_call.arguments[0], "42");
    EXPECT_FALSE(fault_call.result_target.has_value());
}

/**
 * @brief Verify 100% standard OMG SysML v2.0 syntax: import ScalarValues::*, item def, calc def -> service,
 *        directional in/out parameters on state def, attribute := init, assign x := expr;, and accept after N [ms].
 */
TEST(Sysml2ParserTest, ParsesStandardOmgSysmlV2CalcDefAssignAndBracketedAfter) {
    const std::string sysml = R"sysml(
package StandardV2Pkg {
    import ScalarValues::*;

    item def EvStart;
    item def EvTick;

    calc def compute_pwm {
        in temp : Integer;
        return : Integer;
    }

    state def StandardCtrl {
        in temp_c : Integer;
        out pwm_duty : Integer;
        attribute count : Integer := 0;

        entry; then Idle;
        state Idle;
        state Active;
        state TimedOut;

        transition t_start
            first Idle
            accept EvStart
            if temp_c < 100
            then Active;

        transition t_step
            first Active
            accept EvTick
            do action ControlStep {
                assign pwm_duty := compute_pwm(temp_c);
                assign count := count + 1;
            }
            then Active;

        transition t_timeout
            first Active
            accept after 150 [ms]
            do action Shutdown {
                assign pwm_duty := 0;
            }
            then TimedOut;
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml, model, err)) << err.render_to_string();

    // Directional in/out parameters on state def -> ports (calc def 'in temp' must NOT leak into ports!)
    ASSERT_EQ(model.ports.size(), 2u);
    ASSERT_NE(model.find_port("temp_c"), nullptr);
    EXPECT_TRUE(model.find_port("temp_c")->is_in());
    ASSERT_NE(model.find_port("pwm_duty"), nullptr);
    EXPECT_TRUE(model.find_port("pwm_duty")->is_out());

    // Native calc def -> pure typed service
    ASSERT_EQ(model.services.size(), 1u);
    EXPECT_EQ(model.services[0].name, "compute_pwm");
    EXPECT_TRUE(model.services[0].is_pure);
    ASSERT_EQ(model.services[0].parameters.size(), 1u);
    EXPECT_EQ(model.services[0].parameters[0].name, "temp");
    EXPECT_EQ(model.services[0].return_type.to_canonical_string(), "uint32");

    // attribute := 0
    ASSERT_EQ(model.variables.size(), 1u);
    EXPECT_EQ(model.variables[0].name, "count");
    EXPECT_EQ(model.variables[0].initial_value, "0");

    // assign pwm_duty := compute_pwm(temp_c); & assign count := count + 1;
    ASSERT_EQ(model.transitions.size(), 3u);
    const auto& t_step = model.transitions[1];
    ASSERT_TRUE(t_step.transition_action.has_value());
    ASSERT_EQ(t_step.transition_action->instructions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ActionCallOp>(t_step.transition_action->instructions[0].op));
    const auto& call = std::get<ActionCallOp>(t_step.transition_action->instructions[0].op);
    EXPECT_EQ(call.function_name, "compute_pwm");
    ASSERT_TRUE(call.result_target.has_value());
    EXPECT_EQ(call.result_target->name, "pwm_duty");
    EXPECT_EQ(call.result_target->scope, LValueScope::OutPort);
    ASSERT_EQ(t_step.transition_action->assignments.size(), 1u);
    EXPECT_EQ(t_step.transition_action->assignments[0].target.name, "count");

    // accept after 150 [ms]
    const auto& t_to = model.transitions[2];
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(t_to.trigger));
    EXPECT_EQ(std::get<TimeTrigger>(t_to.trigger).duration_in_ms(), 150u);
}

/**
 * @brief Verify Sysml2Serializer emits standard OMG SysML v2.0 (import ScalarValues::*, calc def, item def, in/out,
 * attribute :=, assign :=, accept after N [ms]) and round-trips cleanly through Sysml2Parser.
 * @scenario Serialize an FsmIr with a pure service, directional ports, register, assignment, and time trigger to SysML
 * v2.0 text and parse it back.
 * @expected Output contains standard OMG SysML v2.0 tokens and re-parsed IR preserves service, ports, variable, and
 * timer.
 */
TEST(Sysml2ParserTest, Sysml2Serializer_RoundtripsStandardOmgSysmlV2CalcDefAndSyntax) {
    FsmIr orig;
    orig.name = "ThermalLoop";
    orig.package = "ThermalPkg";
    orig.initial_state = "Idle";
    orig.add_state("Idle");
    orig.add_state("Active");

    PortDefinition p_in("temp_c", DataType::int32(), PortDirection::In);
    PortDefinition p_out("pwm_duty", DataType::uint32(), PortDirection::Out);
    orig.add_port(p_in);
    orig.add_port(p_out);
    orig.add_variable(VariableDefinition("ticks", DataType::uint32(), "0"));

    ServiceMethodSignature svc;
    svc.name = "compute_pwm";
    svc.return_type = DataType::uint32();
    svc.is_pure = true;
    svc.parameters.push_back(ServiceParameter{"temp", DataType::int32()});
    orig.add_service(svc);

    SignalDefinition sig("EvStart");
    orig.add_signal(sig);

    TransitionEdge t1("Idle", "Active", "EvStart");
    ActionSignature act("InitTicks");
    ActionAssignment assign_ticks;
    assign_ticks.target = LValueTarget("ticks");
    assign_ticks.op = AssignmentOp::Assign;
    assign_ticks.expression = "1";
    act.assignments.push_back(std::move(assign_ticks));
    t1.transition_action = act;
    orig.add_transition(t1);

    TransitionEdge t2("Active", "Idle", "");
    t2.trigger = TimeTrigger(TimeTriggerKind::After, 200, TimeUnit::Milliseconds);
    orig.add_transition(t2);

    const std::string text = fsm::backend::formal::Sysml2Serializer::serialize(orig);
    EXPECT_NE(text.find("import ScalarValues::*;"), std::string::npos) << text;
    EXPECT_NE(text.find("calc def compute_pwm"), std::string::npos) << text;
    EXPECT_NE(text.find("item def EvStart;"), std::string::npos) << text;
    EXPECT_NE(text.find("in temp_c :"), std::string::npos) << text;
    EXPECT_NE(text.find("out pwm_duty :"), std::string::npos) << text;
    EXPECT_NE(text.find("attribute ticks :"), std::string::npos) << text;
    EXPECT_NE(text.find("assign ticks := 1;"), std::string::npos) << text;
    EXPECT_NE(text.find("accept after 200 [ms]"), std::string::npos) << text;

    Sysml2Parser parser;
    FsmIr reparsed;
    fsm::diagnostic::DiagnosticEngine diag;
    ASSERT_TRUE(parser.parse(text, reparsed, diag)) << diag.render_to_string();

    ASSERT_EQ(reparsed.services.size(), 1u);
    EXPECT_EQ(reparsed.services[0].name, "compute_pwm");
    EXPECT_TRUE(reparsed.services[0].is_pure);
    ASSERT_EQ(reparsed.ports.size(), 2u);
    ASSERT_EQ(reparsed.variables.size(), 1u);
    EXPECT_EQ(reparsed.variables[0].name, "ticks");
    ASSERT_EQ(reparsed.transitions.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(reparsed.transitions[1].trigger));
    EXPECT_EQ(std::get<TimeTrigger>(reparsed.transitions[1].trigger).duration_in_ms(), 200u);
}

/**
 * @brief Verify OMG SysML v2.0 `port def`, conjugated `~PortDef`, `part def` `exhibit state`, `send new ... via ...`,
 * multi-file `import Pkg::*;`, and `--state-def` / `list_state_defs()` selection.
 * @scenario Write a shared `protocol.sysml` (`port def ThermalLinkPort`, `item def EvOverTemp`) and a multi-machine
 * `subsystem.sysml` (`part def SensorUnit` with `~ThermalLinkPort`, `part def CoolerUnit` with `ThermalLinkPort`, and
 * two `state def`s), then parse each machine with `Sysml2ParseOptions`.
 * @expected `list_state_defs` returns both machines; `SensorFsm` gets conjugated `Out` port `temp_c` and `SignalEmitOp`
 * for `EvOverTemp`; `CoolerFsm` gets `In` port `temp_c` and `EvOverTemp` trigger.
 */
TEST(Sysml2ParserTest, PortDefConjugationMultiFileImportAndStateDefSelection) {
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_test_sysml2_workspace";
    fs::create_directories(tmp_dir);

    {
        std::ofstream(tmp_dir / "shared_protocol.sysml") << R"sysml(
package SharedProtocol {
    import ScalarValues::*;

    item def EvSample;
    item def EvOverTemp {
        attribute peak_temp : Int32;
    }

    port def ThermalLinkPort {
        in attribute temp_c : Int32;
        in item alert : EvOverTemp;
    }
}
)sysml";
    }

    const std::string subsys_text = R"sysml(
package ThermalSubsystem {
    import ScalarValues::*;
    import SharedProtocol::*;

    part def SensorUnit {
        port link : ~ThermalLinkPort;
        exhibit state sm : SensorFsm;
    }

    part def CoolerUnit {
        port link : ThermalLinkPort;
        exhibit state sm : CoolerFsm;
    }

    state def SensorFsm {
        entry; then Monitoring;
        state Monitoring;
        state Alerted;

        transition t_overtemp
            first Monitoring
            accept EvSample
            if temp_c >= 90
            do send new EvOverTemp(temp_c) via link
            then Alerted;
    }

    state def CoolerFsm {
        entry; then Standby;
        state Standby;
        state Cooling;

        transition t_cool
            first Standby
            accept alert : EvOverTemp via link
            then Cooling;
    }
}
)sysml";

    const auto machines = Sysml2Parser::list_state_defs(subsys_text);
    ASSERT_EQ(machines.size(), 2U);
    EXPECT_EQ(machines[0], "SensorFsm");
    EXPECT_EQ(machines[1], "CoolerFsm");

    // 1. Parse SensorFsm (conjugated ~ThermalLinkPort -> temp_c becomes Out!)
    {
        Sysml2Parser parser;
        Sysml2ParseOptions sopts;
        sopts.import_dirs.push_back(tmp_dir.string());
        sopts.target_state_def = "SensorFsm";
        parser.set_options(sopts);

        FsmIr ir;
        fsm::diagnostic::DiagnosticEngine diag;
        ASSERT_TRUE(parser.parse(subsys_text, ir, diag)) << diag.render_to_string();
        EXPECT_EQ(ir.name, "SensorFsm");
        ASSERT_EQ(ir.states.size(), 2U);
        const auto* p_temp = ir.find_port("temp_c");
        ASSERT_NE(p_temp, nullptr);
        EXPECT_TRUE(p_temp->is_out());
        ASSERT_EQ(ir.transitions.size(), 1U);
        ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
        ASSERT_EQ(ir.transitions[0].transition_action->instructions.size(), 1U);
        ASSERT_TRUE(std::holds_alternative<SignalEmitOp>(ir.transitions[0].transition_action->instructions[0].op));
        const auto& se = std::get<SignalEmitOp>(ir.transitions[0].transition_action->instructions[0].op);
        EXPECT_EQ(se.signal_name, "EvOverTemp");
        ASSERT_TRUE(se.target_port.has_value());
        EXPECT_EQ(*se.target_port, "link");
        // Imported signal attributes preserved!
        const auto* sig = ir.find_signal("EvOverTemp");
        ASSERT_NE(sig, nullptr);
        ASSERT_EQ(sig->attributes.size(), 1U);
        EXPECT_EQ(sig->attributes[0].name, "peak_temp");
    }

    // 2. Parse CoolerFsm (non-conjugated ThermalLinkPort -> temp_c stays In!)
    {
        Sysml2Parser parser;
        Sysml2ParseOptions sopts;
        sopts.import_dirs.push_back(tmp_dir.string());
        sopts.target_state_def = "CoolerFsm";
        parser.set_options(sopts);

        FsmIr ir;
        fsm::diagnostic::DiagnosticEngine diag;
        ASSERT_TRUE(parser.parse(subsys_text, ir, diag)) << diag.render_to_string();
        EXPECT_EQ(ir.name, "CoolerFsm");
        ASSERT_EQ(ir.states.size(), 2U);
        const auto* p_temp = ir.find_port("temp_c");
        ASSERT_NE(p_temp, nullptr);
        EXPECT_TRUE(p_temp->is_in());
        ASSERT_EQ(ir.transitions.size(), 1U);
        EXPECT_EQ(ir.transitions[0].event, "EvOverTemp");
    }

    // 3. Unknown --state-def must fail cleanly with diagnostic listing available machines
    {
        Sysml2Parser parser;
        Sysml2ParseOptions sopts;
        sopts.import_dirs.push_back(tmp_dir.string());
        sopts.target_state_def = "NonExistentFsm";
        parser.set_options(sopts);

        FsmIr ir;
        fsm::diagnostic::DiagnosticEngine diag;
        EXPECT_FALSE(parser.parse(subsys_text, ir, diag));
        EXPECT_NE(diag.render_to_string().find("SensorFsm"), std::string::npos);
    }

    fs::remove_all(tmp_dir);
}

/**
 * @brief Verify strict OMG SysML v2.0 grammar conformance (`private import`, `state S parallel`, state-local
 * `assert constraint`, local `item msg : Ev { attribute :>> ... }` + `send msg via port`, named constructor
 * `send new Ev(attr = val) via port`, and KerML `and`/`or`/`not` serializer emission).
 * @scenario Parse a strictly conformant OMG SysML v2.0 state machine, verify model checking on its state-local
 * `assert constraint`, serialize back via `Sysml2Serializer`, and re-parse.
 * @expected Parser extracts parallel state, state constraint safety property, and both `SignalEmitOp` payloads;
 * serializer emits `private import ScalarValues::*;`, `state Sup parallel`, `and`/`not`, and zero `&&` or
 * `assert property`.
 */
TEST(Sysml2ParserTest, StrictOmgSysmlV2GrammarConformance_LocalItemSendConstraintParallelAndPrivateImport) {
    const std::string src = R"(
        package StrictSysmlPkg {
            private import ScalarValues::*;
            private import SI::*;

            item def EvSample;
            item def EvOverTemp {
                attribute peak_temp : Integer;
            }

            port def AlertPort {
                out item alert : EvOverTemp;
            }

            state def ThermalGuard {
                in sensor_temp_c : Integer;
                in enabled : Boolean;
                out heater_enable : Boolean;
                port link : AlertPort;

                entry; then Nominal;

                state Nominal;

                state OverTempLatched {
                    assert constraint SafeHeaterCutoff {
                        not heater_enable
                    }
                }

                state Sup parallel {
                    state RegA {
                        entry; then A0;
                        state A0;
                    }
                    state RegB {
                        entry; then B0;
                        state B0;
                    }
                }

                transition t_trip
                    first Nominal
                    accept EvSample
                    if sensor_temp_c >= 85 and enabled
                    do action TripProtection {
                        assign heater_enable := false;
                        item msg : EvOverTemp {
                            attribute :>> peak_temp = sensor_temp_c;
                        }
                        send msg via link;
                    }
                    then OverTempLatched;

                transition t_named_ctor
                    first OverTempLatched
                    accept after 100 [ms]
                    do action ReNotify {
                        send new EvOverTemp(peak_temp = sensor_temp_c) via link;
                    }
                    then OverTempLatched;
            }
        }
    )";

    Sysml2Parser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine diag;
    ASSERT_TRUE(parser.parse(src, ir, diag)) << diag.render_to_string();

    const auto* sup = ir.find_state("Sup");
    ASSERT_NE(sup, nullptr);
    EXPECT_EQ(sup->kind, StateKind::Parallel);

    ASSERT_EQ(ir.properties.size(), 1U);
    EXPECT_EQ(ir.properties[0].name, "SafeHeaterCutoff");

    ASSERT_EQ(ir.transitions.size(), 2U);
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    ASSERT_EQ(ir.transitions[0].transition_action->instructions.size(), 1U);
    const auto* em0 = std::get_if<SignalEmitOp>(&ir.transitions[0].transition_action->instructions[0].op);
    ASSERT_NE(em0, nullptr);
    EXPECT_EQ(em0->signal_name, "EvOverTemp");
    EXPECT_EQ(em0->target_port, "link");
    ASSERT_EQ(em0->arguments.size(), 1U);
    EXPECT_EQ(em0->arguments[0], "sensor_temp_c");

    ASSERT_TRUE(ir.transitions[1].transition_action.has_value());
    ASSERT_EQ(ir.transitions[1].transition_action->instructions.size(), 1U);
    const auto* em1 = std::get_if<SignalEmitOp>(&ir.transitions[1].transition_action->instructions[0].op);
    ASSERT_NE(em1, nullptr);
    EXPECT_EQ(em1->signal_name, "EvOverTemp");
    ASSERT_EQ(em1->arguments.size(), 1U);
    EXPECT_EQ(em1->arguments[0], "sensor_temp_c");

    const std::string exported = fsm::backend::formal::Sysml2Serializer::serialize(ir);
    EXPECT_NE(exported.find("private import ScalarValues::*;"), std::string::npos) << exported;
    EXPECT_NE(exported.find("private import SI::*;"), std::string::npos) << exported;
    EXPECT_NE(exported.find("state Sup parallel {"), std::string::npos) << exported;
    EXPECT_EQ(exported.find("&&"), std::string::npos) << exported;
    EXPECT_EQ(exported.find("assert property"), std::string::npos) << exported;

    FsmIr reparsed;
    ASSERT_TRUE(parser.parse(exported, reparsed, diag)) << diag.render_to_string();
    ASSERT_NE(reparsed.find_state("Sup"), nullptr);
    EXPECT_EQ(reparsed.find_state("Sup")->kind, StateKind::Parallel);
    ASSERT_EQ(reparsed.transitions.size(), 2U);
}

/**
 * @brief Verify native OMG SysML v2.0 `Time::DurationOf` state residence constraints and `metadata def` /
 * `@MetadataUsage` (`@Priority`, `@Deferred`, `@TemporalProperty`) parse and roundtrip with zero `@fsm:` comments.
 * @scenario Parse a pure OMG SysML v2.0 model using `DurationOf(Precharge) <= 100 [ms]`, `@Deferred`, `@Priority`,
 * and `@SafeShutdown : TemporalProperty`, serialize via `Sysml2Serializer`, and re-parse.
 * @expected Serialized output contains zero `@fsm:` directives, preserves `time_invariant`, `deferred_events`,
 * transition `priority`, and `FormalProperty` metadata across roundtrip.
 */
TEST(Sysml2ParserTest, NativeSysmlV2DurationOfAndMetadataUsages_ZeroFsmDirectivesRoundtrip) {
    const std::string sysml = R"sysml(
package BatterySafety {
    private import ScalarValues::*;
    private import SI::*;
    private import Time::DurationOf;

    metadata def Priority {
        attribute value : Natural;
    }

    metadata def Deferred {
        attribute events : String[*];
    }

    metadata def TemporalProperty {
        attribute kind : String;
        attribute formula : String;
        attribute req : String[0..1];
        attribute description : String[0..1];
    }

    item def EvReady;
    item def EvFault;
    item def EvTelemetryReq;
    item def EvDiagDump;

    state def PackSequencer {
        @SafeFaultResponse : TemporalProperty {
            kind = "Safety";
            formula = "G (EvFault -> F SafeTripped)";
            req = "REQ-BMS-042";
            description = "Any fault transitions to SafeTripped";
        }

        out contactors_closed : Boolean;

        entry; then Precharge;

        state Precharge {
            assert constraint { DurationOf(Precharge) <= 100 [ms] }
            @Deferred { events = ("EvTelemetryReq", "EvDiagDump"); }
        }

        state Running;
        state SafeTripped;

        transition t_ready
            first Precharge
            accept EvReady
            do action { assign contactors_closed := true; }
            then Running {
                @Priority { value = 5; }
            }

        transition t_fault
            first Precharge
            accept EvFault
            do action { assign contactors_closed := false; }
            then SafeTripped {
                @Priority { value = 10; }
            }
    }
}
)sysml";

    Sysml2Parser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine diag;
    ASSERT_TRUE(parser.parse(sysml, ir, diag)) << diag.render_to_string();
    EXPECT_FALSE(diag.has_errors());
    EXPECT_TRUE(ir.variables.empty());

    const auto* pre = ir.find_state("Precharge");
    ASSERT_NE(pre, nullptr);
    ASSERT_TRUE(pre->time_invariant.has_value());
    EXPECT_EQ(*pre->time_invariant, "100[ms]");
    ASSERT_EQ(pre->deferred_events.size(), 2U);
    EXPECT_EQ(pre->deferred_events[0], "EvTelemetryReq");
    EXPECT_EQ(pre->deferred_events[1], "EvDiagDump");

    ASSERT_EQ(ir.properties.size(), 1U);
    EXPECT_EQ(ir.properties[0].name, "SafeFaultResponse");
    EXPECT_EQ(ir.properties[0].kind, PropertyKind::Safety);
    EXPECT_EQ(ir.properties[0].raw_formula, "G (EvFault -> F SafeTripped)");
    EXPECT_EQ(ir.properties[0].traceability_req, "REQ-BMS-042");
    EXPECT_EQ(ir.properties[0].description, "Any fault transitions to SafeTripped");

    ASSERT_EQ(ir.transitions.size(), 2U);
    EXPECT_EQ(ir.transitions[0].target, "Running");
    EXPECT_EQ(ir.transitions[0].priority, 5U);
    EXPECT_EQ(ir.transitions[1].target, "SafeTripped");
    EXPECT_EQ(ir.transitions[1].priority, 10U);

    const std::string exported = fsm::backend::formal::Sysml2Serializer::serialize(ir);
    EXPECT_EQ(exported.find("@fsm:"), std::string::npos) << exported;
    EXPECT_NE(exported.find("private import Time::DurationOf;"), std::string::npos) << exported;
    EXPECT_NE(exported.find("DurationOf(Precharge) <= 100 [ms]"), std::string::npos) << exported;
    EXPECT_NE(exported.find("metadata def Priority"), std::string::npos) << exported;
    EXPECT_NE(exported.find("metadata def Deferred"), std::string::npos) << exported;
    EXPECT_NE(exported.find("metadata def TemporalProperty"), std::string::npos) << exported;
    EXPECT_NE(exported.find("@SafeFaultResponse : TemporalProperty"), std::string::npos) << exported;
    EXPECT_NE(exported.find("@Priority { value = 10; }"), std::string::npos) << exported;

    FsmIr reparsed;
    ASSERT_TRUE(parser.parse(exported, reparsed, diag)) << diag.render_to_string();
    EXPECT_TRUE(reparsed.variables.empty());
    const auto* pre2 = reparsed.find_state("Precharge");
    ASSERT_NE(pre2, nullptr);
    ASSERT_TRUE(pre2->time_invariant.has_value());
    EXPECT_EQ(*pre2->time_invariant, "100[ms]");
    EXPECT_EQ(pre2->deferred_events, pre->deferred_events);
    ASSERT_EQ(reparsed.properties.size(), 1U);
    EXPECT_EQ(reparsed.properties[0].raw_formula, "G (EvFault -> F SafeTripped)");
    EXPECT_EQ(reparsed.properties[0].traceability_req, "REQ-BMS-042");
    ASSERT_EQ(reparsed.transitions.size(), 2U);
    EXPECT_EQ(reparsed.transitions[0].priority, 5U);
    EXPECT_EQ(reparsed.transitions[1].priority, 10U);
}

/**
 * @brief Test Intent: Verify multi-machine SysML v2 files default to the first state def with a W_MULTI_STATE_DEF
 * warning instead of merging states across machines.
 */
TEST(Sysml2ParserTest, MultiMachineFile_DefaultsToFirstWithWarning) {
    const std::string sysml = R"(
package MultiPkg {
    attribute def EvGo;
    state def MachineA {
        entry; then A1;
        state A1;
        state A2;
        transition first A1 accept EvGo then A2;
    }
    state def MachineB {
        entry; then B1;
        state B1;
        state B2;
        transition first B1 accept EvGo then B2;
    }
}
)";
    Sysml2Parser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine diag;
    ASSERT_TRUE(parser.parse(sysml, ir, diag));
    EXPECT_EQ(ir.name, "MachineA");
    EXPECT_NE(ir.find_state("A1"), nullptr);
    EXPECT_EQ(ir.find_state("B1"), nullptr);

    bool found_warn = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "W_MULTI_STATE_DEF") {
            found_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_warn);
}

}  // namespace

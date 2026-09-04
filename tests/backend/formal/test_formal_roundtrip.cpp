#include <gtest/gtest.h>

#include "fsm/backend/diagram/dot_serializer.hpp"
#include "fsm/backend/diagram/json_serializer.hpp"
#include "fsm/backend/diagram/mermaid_serializer.hpp"
#include "fsm/backend/diagram/plantuml_serializer.hpp"
#include "fsm/backend/formal/cameo_serializer.hpp"
#include "fsm/backend/formal/scxml_serializer.hpp"
#include "fsm/backend/formal/smv_serializer.hpp"
#include "fsm/backend/formal/stateflow_serializer.hpp"
#include "fsm/backend/formal/sysml2_serializer.hpp"
#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/frontend/diagram/json_parser.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/frontend/formal/scxml_parser.hpp"
#include "fsm/frontend/formal/smv_parser.hpp"
#include "fsm/frontend/formal/stateflow_parser.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"

using namespace fsm::codegen;

namespace {

void assert_ir_equivalent(const FsmIr& ir1, const FsmIr& ir2, const std::string& path_info) {
    if (!ir1.initial_state.empty() && !ir2.initial_state.empty()) {
        EXPECT_EQ(ir1.initial_state, ir2.initial_state) << path_info;
    }
    EXPECT_EQ(ir1.states.size(), ir2.states.size()) << path_info;

    for (const auto& s1 : ir1.states) {
        const auto* s2 = ir2.find_state(s1.name);
        ASSERT_NE(s2, nullptr) << path_info << " Missing state: " << s1.name;
        EXPECT_EQ(s1.parent_state, s2->parent_state) << path_info << " State: " << s1.name << " parent mismatch";
        EXPECT_EQ(s1.is_composite, s2->is_composite) << path_info << " State: " << s1.name << " composite mismatch";
        if (s1.do_activity.has_value() && !s1.do_activity->empty()) {
            EXPECT_EQ(s1.do_activity, s2->do_activity) << path_info << " State: " << s1.name << " do_activity mismatch";
        }
        if (s1.has_history) {
            EXPECT_EQ(s1.has_history, s2->has_history) << path_info << " State: " << s1.name << " history mismatch";
        }
    }

    EXPECT_EQ(ir1.transitions.size(), ir2.transitions.size()) << path_info;
    for (const auto& t1 : ir1.transitions) {
        bool found = false;
        for (const auto& t2 : ir2.transitions) {
            if (t1.source == t2.source && t1.target == t2.target && t1.event == t2.event) {
                found = true;
                if (!t1.action.value_or("").empty() && !t2.action.value_or("").empty()) {
                    EXPECT_EQ(t1.action, t2.action) << path_info << " Transition " << t1.source << " -> " << t1.target;
                }
                break;
            }
        }
        EXPECT_TRUE(found) << path_info << " Missing transition: " << t1.source << " --(" << t1.event << ")--> "
                           << t1.target;
    }

    EXPECT_EQ(ir1.enums.size(), ir2.enums.size()) << path_info << " Enums size mismatch";
    for (const auto& e1 : ir1.enums) {
        const auto* e2 = ir2.find_enum(e1.name);
        ASSERT_NE(e2, nullptr) << path_info << " Missing enum: " << e1.name;
        EXPECT_EQ(e1.underlying_type, e2->underlying_type)
            << path_info << " Enum: " << e1.name << " underlying type mismatch";
        EXPECT_EQ(e1.literals.size(), e2->literals.size())
            << path_info << " Enum: " << e1.name << " literals count mismatch";
        for (size_t li = 0; li < e1.literals.size(); ++li) {
            EXPECT_EQ(e1.literals[li].name, e2->literals[li].name) << path_info;
            if (e1.literals[li].value.has_value() && e2->literals[li].value.has_value()) {
                EXPECT_EQ(*e1.literals[li].value, *e2->literals[li].value) << path_info;
            }
        }
    }

    EXPECT_EQ(ir1.structs.size(), ir2.structs.size()) << path_info << " Structs size mismatch";
    for (const auto& s1 : ir1.structs) {
        const auto* s2 = ir2.find_struct(s1.name);
        ASSERT_NE(s2, nullptr) << path_info << " Missing struct: " << s1.name;
        EXPECT_EQ(s1.is_datatype, s2->is_datatype)
            << path_info << " Struct: " << s1.name << " is_datatype mismatch";
        EXPECT_EQ(s1.fields.size(), s2->fields.size())
            << path_info << " Struct: " << s1.name << " fields count mismatch";
        for (size_t fi = 0; fi < s1.fields.size(); ++fi) {
            EXPECT_EQ(s1.fields[fi].name, s2->fields[fi].name) << path_info;
            EXPECT_EQ(s1.fields[fi].type, s2->fields[fi].type) << path_info;
            if (!s1.fields[fi].default_value.empty() && !s2->fields[fi].default_value.empty()) {
                EXPECT_EQ(s1.fields[fi].default_value, s2->fields[fi].default_value) << path_info;
            }
        }
    }
}

void verify_roundtrip(const FsmIr& baseline) {
    std::string err;

    // 1. Mermaid
    const std::string mmd = MermaidSerializer::serialize(baseline);
    MermaidParser mmd_parser;
    FsmIr mmd_ir;
    ASSERT_TRUE(mmd_parser.parse(mmd, mmd_ir, err)) << "Mermaid parse error: " << err;
    assert_ir_equivalent(baseline, mmd_ir, "Mermaid roundtrip");

    // 2. PlantUML
    const std::string puml = PlantUmlSerializer::serialize(baseline);
    PlantUmlParser puml_parser;
    FsmIr puml_ir;
    ASSERT_TRUE(puml_parser.parse(puml, puml_ir, err)) << "PlantUML parse error: " << err;
    assert_ir_equivalent(baseline, puml_ir, "PlantUML roundtrip");

    // 3. SysML v2
    const std::string sysml = Sysml2Serializer::serialize(baseline);
    Sysml2Parser sysml_parser;
    FsmIr sysml_ir;
    ASSERT_TRUE(sysml_parser.parse(sysml, sysml_ir, err)) << "SysML v2 parse error: " << err;
    assert_ir_equivalent(baseline, sysml_ir, "SysML v2 roundtrip");

    // 4. XState JSON
    const std::string json_str = JsonSerializer::serialize(baseline);
    JsonStateParser json_parser;
    FsmIr json_ir;
    ASSERT_TRUE(json_parser.parse(json_str, json_ir, err)) << "JSON parse error: " << err;
    assert_ir_equivalent(baseline, json_ir, "JSON roundtrip");

    // 5. Graphviz DOT
    const std::string dot_str = DotSerializer::serialize(baseline);
    DotParser dot_parser;
    FsmIr dot_ir;
    ASSERT_TRUE(dot_parser.parse(dot_str, dot_ir, err)) << "DOT parse error: " << err;
    assert_ir_equivalent(baseline, dot_ir, "DOT roundtrip");

    // 6. W3C SCXML
    const std::string scxml_str = ScxmlSerializer::serialize(baseline);
    ScxmlParser scxml_parser;
    FsmIr scxml_ir;
    ASSERT_TRUE(scxml_parser.parse(scxml_str, scxml_ir, err)) << "SCXML parse error: " << err;
    assert_ir_equivalent(baseline, scxml_ir, "SCXML roundtrip");

    // 7. Cameo / MagicDraw OMG XMI 2.1
    const std::string xmi_str = CameoSerializer::serialize(baseline);
    CameoXmiParser xmi_parser;
    FsmIr xmi_ir;
    ASSERT_TRUE(xmi_parser.parse(xmi_str, xmi_ir, err)) << "Cameo XMI parse error: " << err;
    assert_ir_equivalent(baseline, xmi_ir, "Cameo XMI roundtrip");

    // 8. MathWorks Simulink Stateflow XML
    const std::string sf_str = StateflowSerializer::serialize(baseline);
    StateflowParser sf_parser;
    FsmIr sf_ir;
    ASSERT_TRUE(sf_parser.parse(sf_str, sf_ir, err)) << "Stateflow parse error: " << err;
    assert_ir_equivalent(baseline, sf_ir, "Stateflow roundtrip");
}

/**
 * @brief Test Intent: Verify lossless roundtrip serialization across all 7 supported diagram/schema formats.
 *
 * Scenario:
 * - Build baseline FsmIr from ConnectionManager model.
 * - Serialize to Mermaid, PlantUML, SysML v2, JSON, DOT, SCXML, Cameo XMI.
 * - Parse each emitted format back to FsmIr and assert structural equality.
 */
TEST(LosslessRoundtripTest, ConnectionManagerPreset) {
    const std::string puml = R"(
    @startuml
    [*] --> Disconnected

    Disconnected --> Connecting : ConnectCmd [HasNetworkGuard && HasValidCredentialsGuard] / InitSocketAction
    Disconnected --> Disconnected : ConnectCmd [!HasNetworkGuard || !HasValidCredentialsGuard] / LogErrorAction
    Connecting --> Connected : HandshakeOkEvent / SetupSessionAction
    Connecting --> Disconnected : HandshakeFailedEvent / CleanupAction
    Connecting --> Disconnected : TimeoutEvent / CleanupAction
    Connected --> Suspended : NetworkDegradedEvent / PauseQueueAction
    Connected --> Disconnected : DisconnectCmd / CloseSocketAction
    Connected --> Disconnected : ConnectionLostEvent / CleanupAction
    Suspended --> Connected : NetworkRestoredEvent / ResumeQueueAction
    Suspended --> Disconnected : DisconnectCmd / CloseSocketAction
    Suspended --> Disconnected : TimeoutEvent / CleanupAction
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr baseline;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, baseline, err)) << "Parse error: " << err;
    EXPECT_EQ(baseline.states.size(), 4U);
    EXPECT_EQ(baseline.transitions.size(), 11U);

    verify_roundtrip(baseline);
}

/**
 * @brief Test Intent: Verify lossless multi-format roundtrip for Async Motor Controller preset.
 *
 * Scenario:
 * - 5-state motor controller with regenerative braking and overcurrent fault transitions.
 * - Verify all 7 format roundtrips preserve state graph topology.
 */
TEST(LosslessRoundtripTest, AsyncMotorControllerPreset) {
    const std::string puml = R"(
    @startuml
    [*] --> Halted

    Halted --> Accelerating : StartCmd [HasValidRpmGuard] / PowerOnInverterAction
    Accelerating --> RunningAtSpeed : SpeedReachedEvent / EngageSpeedPidAction
    RunningAtSpeed --> Decelerating : StopCmd / ApplyRegenerativeBrakeAction
    Decelerating --> Halted : StoppedEvent / DisengageInverterAction
    Accelerating --> Faulted : OvercurrentEvent / EmergencyCutoffAction
    RunningAtSpeed --> Faulted : OvercurrentEvent / EmergencyCutoffAction
    Decelerating --> Faulted : OvercurrentEvent / EmergencyCutoffAction
    Faulted --> Halted : ResetFaultCmd [IsThermalSafeGuard] / ClearFaultFlagsAction
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr baseline;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, baseline, err)) << "Parse error: " << err;
    EXPECT_EQ(baseline.states.size(), 5U);
    EXPECT_EQ(baseline.transitions.size(), 8U);

    verify_roundtrip(baseline);
}

/**
 * @brief Test Intent: Verify lossless multi-format roundtrip for Aerospace Mission Controller preset.
 *
 * Scenario:
 * - 7-state mission controller with flight phases, abort branches, and panel deployments.
 * - Verify roundtrip fidelity across all serializers.
 */
TEST(LosslessRoundtripTest, MissionControllerPreset) {
    const std::string puml = R"(
    @startuml
    [*] --> Standby

    Standby --> Ascending : LaunchCmd [ValidClearanceGuard] / ArmEnginesAction
    Ascending --> Cruising : AltitudeReachedEvent / DeploySolarPanelsAction
    Cruising --> Orbiting : OrbitInsertedEvent / StabilizeAttitudeAction
    Orbiting --> Landing : ReturnHomeCmd / RetractPanelsAction
    Landing --> MissionCompleted : TouchdownEvent / ShutdownSystemsAction
    Ascending --> Aborted : AbortCmd / TriggerAlarmAction
    Cruising --> Aborted : AbortCmd / TriggerAlarmAction
    Orbiting --> Aborted : AbortCmd / TriggerAlarmAction
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr baseline;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, baseline, err)) << "Parse error: " << err;
    EXPECT_EQ(baseline.states.size(), 7U);
    EXPECT_EQ(baseline.transitions.size(), 8U);

    verify_roundtrip(baseline);
}

/**
 * @brief Test Intent: Verify lossless multi-format roundtrip for Industrial Press controller.
 *
 * Scenario:
 * - 6-state industrial machine with automated and manual controls.
 * - Verify all formats preserve transitions, guards, and action bindings.
 */
TEST(LosslessRoundtripTest, IndustrialPressPreset) {
    const std::string puml = R"(
    @startuml
    [*] --> Idle

    Idle --> Initializing : PowerOnCmd [SafetyOk] / LogPowerOn
    Initializing --> Ready : InitDone / StoreDiagnostics
    Initializing --> Idle : AbortCmd / Cleanup
    Ready --> Running : StartCmd [ToolLoaded] / EngageDrive
    Running --> Paused : PauseCmd / HoldPosition
    Paused --> Running : ResumeCmd [SafetyOk] / ReleaseHold
    Running --> Faulted : EStopEvent / EmergencyBrake
    Faulted --> Idle : ResetFaultCmd / ClearFault
    Running --> Idle : StopCmd / DisengageDrive
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr baseline;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, baseline, err)) << "Parse error: " << err;
    EXPECT_EQ(baseline.states.size(), 6U);
    EXPECT_EQ(baseline.transitions.size(), 9U);

    verify_roundtrip(baseline);
}

/**
 * @brief Test Intent: Verify OMG SysML v2 syntax parsing and lossless 7-format roundtrip.
 *
 * Scenario:
 * - Parse SpacecraftController defined in native SysML v2 syntax.
 * - Verify roundtrip equality across all format serializers.
 */
TEST(LosslessRoundtripTest, Sysml2SpacecraftPreset) {
    const std::string sysml = R"(
    state def SpacecraftController {
        initial state Standby;
        
        state Standby;
        state InFlight;
        state Landing;
        state MissionCompleted;
        state Aborted;

        transition from Standby accept AuthorizeCmd if ValidClearanceGuard then InFlight;
        transition from InFlight accept ReturnHomeCmd then Landing;
        transition from Landing accept TouchdownEvent then MissionCompleted;
        transition from InFlight accept EmergencyAbortCmd then Aborted;
    }
    )";

    Sysml2Parser parser;
    FsmIr baseline;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml, baseline, err)) << "Parse error: " << err;
    EXPECT_EQ(baseline.states.size(), 5U);
    EXPECT_EQ(baseline.transitions.size(), 4U);

    verify_roundtrip(baseline);
}

/**
 * @brief Test Intent: Verify nested composite states and deferred event list preservation during multi-format
 * roundtrips.
 *
 * Scenario:
 * - Parse 3-level deep hierarchy with deferred events (`defer EvSensor`).
 * - Serialize to Mermaid, SysML v2, SCXML and verify nested states and deferred lists are retained.
 */
TEST(LosslessRoundtripTest, DeepHierarchyAndDeferredEvents) {
    const std::string puml = R"(
    @startuml
    [*] --> Operational

    state Operational {
        [*] --> Manual
        Manual : defer EvSensor
        Manual : defer EvTelemetry

        state Manual {
            [*] --> SlowMode
            state SlowMode
            state FastMode
            SlowMode --> FastMode : AccelerateCmd
            FastMode --> SlowMode : DecelerateCmd
        }

        state Auto {
            state WaypointFollow
            state TargetHold
            WaypointFollow --> TargetHold : TargetAcquired
        }

        Manual --> Auto : EngageAutoPilot
        Auto --> Manual : OverrideCmd
    }

    state Emergency {
        state SafeHalt
    }

    Operational --> Emergency : EStopEvent
    Emergency --> Operational : ResetCmd
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr baseline;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, baseline, err)) << "Parse error: " << err;
    EXPECT_EQ(baseline.states.size(), 9U);
    EXPECT_EQ(baseline.transitions.size(), 7U);

    // Verify deferred events are parsed
    const auto* manual_st = baseline.find_state("Manual");
    ASSERT_NE(manual_st, nullptr);
    EXPECT_EQ(manual_st->deferred_events.size(), 2U);

    // Verify PlantUML, Mermaid, SysML v2, JSON, SCXML
    const std::string mmd = MermaidSerializer::serialize(baseline);
    MermaidParser mmd_p;
    FsmIr mmd_ir;
    ASSERT_TRUE(mmd_p.parse(mmd, mmd_ir, err)) << "Mermaid error: " << err;
    EXPECT_EQ(mmd_ir.states.size(), 9U);

    const std::string sysml = Sysml2Serializer::serialize(baseline);
    Sysml2Parser sysml_p;
    FsmIr sysml_ir;
    ASSERT_TRUE(sysml_p.parse(sysml, sysml_ir, err)) << "SysML v2 error: " << err;
    EXPECT_EQ(sysml_ir.states.size(), 9U);

    const std::string scxml = ScxmlSerializer::serialize(baseline);
    ScxmlParser scxml_p;
    FsmIr scxml_ir;
    ASSERT_TRUE(scxml_p.parse(scxml, scxml_ir, err)) << "SCXML error: " << err;
    EXPECT_EQ(scxml_ir.states.size(), 9U);
}

/**
 * @brief Test Intent: Verify shallow `[H]` and deep `[H*]` history pseudostate roundtrip serialization.
 *
 * Scenario:
 * - Transitions target `Active[H]` and `Active[H*]`.
 * - Verify target_is_history and target_is_deep_history flags are preserved in serializers.
 */
TEST(LosslessRoundtripTest, ShallowAndDeepHistory) {
    const std::string puml = R"(
    @startuml
    [*] --> Standby

    state Active {
        [*] --> Step1
        state Step1
        state Step2
        Step1 --> Step2 : NextStep
    }

    Standby --> Active : StartNormal
    Standby --> Active[H] : ResumeShallow
    Standby --> Active[H*] : ResumeDeep
    Active --> Standby : PauseCmd
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr baseline;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, baseline, err)) << "Parse error: " << err;
    EXPECT_EQ(baseline.states.size(), 4U);
    EXPECT_EQ(baseline.transitions.size(), 5U);

    // Verify history target flags
    bool found_shallow = false;
    bool found_deep = false;
    for (const auto& t : baseline.transitions) {
        if (t.event == "ResumeShallow") {
            found_shallow = t.target_is_history && !t.target_is_deep_history;
        }
        if (t.event == "ResumeDeep") {
            found_deep = t.target_is_history && t.target_is_deep_history;
        }
    }
    EXPECT_TRUE(found_shallow);
    EXPECT_TRUE(found_deep);

    // Verify roundtrip to Mermaid
    const std::string mmd = MermaidSerializer::serialize(baseline);
    MermaidParser mmd_p;
    FsmIr mmd_ir;
    ASSERT_TRUE(mmd_p.parse(mmd, mmd_ir, err)) << "Mermaid history error: " << err;
    EXPECT_EQ(mmd_ir.states.size(), 4U);
    EXPECT_EQ(mmd_ir.transitions.size(), 5U);
}

/**
 * @brief Test Intent: Verify complex compound boolean guard expressions (`&&`, `||`, `!`) across format roundtrips.
 *
 * Scenario:
 * - Transitions with guard predicates: `HasTokenGuard && IsAdminGuard && !IsBlacklistedGuard`.
 * - Verify expressions survive parsing, serialization, and re-parsing losslessly.
 */
TEST(LosslessRoundtripTest, ComplexBooleanGuards) {
    const std::string puml = R"(
    @startuml
    [*] --> Checking

    Checking --> Granted : ReqAccess [HasTokenGuard && IsAdminGuard && !IsBlacklistedGuard] / GrantAccessAction
    Checking --> Degraded : ReqAccess [HasTokenGuard && !IsAdminGuard] / LogAccessAction
    Checking --> Denied : ReqAccess [!HasTokenGuard || IsBlacklistedGuard] / LogSecurityAlertAction
    Granted --> Checking : LogoutCmd / ClearSessionAction
    Degraded --> Checking : LogoutCmd / ClearSessionAction
    Denied --> Checking : RetryCmd / ResetAttemptsAction
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr baseline;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, baseline, err)) << "Parse error: " << err;
    EXPECT_EQ(baseline.states.size(), 4U);
    EXPECT_EQ(baseline.transitions.size(), 6U);

    verify_roundtrip(baseline);
}

/**
 * @brief Test Intent: Verify 7-hop circular conversion ring without data loss (PlantUML -> Mermaid -> SysML2 -> SCXML
 * -> JSON -> DOT -> PlantUML).
 *
 * Scenario:
 * - Serialize through a closed chain of 7 different format representations.
 * - Verify the final reconstructed model is identical to the initial one.
 */
TEST(LosslessRoundtripTest, ClosedLoop7HopFormatRing) {
    const std::string puml_start = R"(
    @startuml
    [*] --> Checking

    Checking --> Granted : ReqAccess [HasTokenGuard && IsAdminGuard && !IsBlacklistedGuard] / GrantAccessAction
    Checking --> Degraded : ReqAccess [HasTokenGuard && !IsAdminGuard] / LogAccessAction
    Checking --> Denied : ReqAccess [!HasTokenGuard || IsBlacklistedGuard] / LogSecurityAlertAction
    Granted --> Checking : LogoutCmd / ClearSessionAction
    Degraded --> Checking : LogoutCmd / ClearSessionAction
    Denied --> Checking : RetryCmd / ResetAttemptsAction
    @enduml
    )";

    std::string err;

    // Hop 1: PlantUML -> Mermaid
    PlantUmlParser puml_parser;
    FsmIr ir1;
    ASSERT_TRUE(puml_parser.parse(puml_start, ir1, err));
    const std::string mmd_str = MermaidSerializer::serialize(ir1);

    // Hop 2: Mermaid -> SysML v2
    MermaidParser mmd_parser;
    FsmIr ir2;
    ASSERT_TRUE(mmd_parser.parse(mmd_str, ir2, err));
    const std::string sysml_str = Sysml2Serializer::serialize(ir2);

    // Hop 3: SysML v2 -> SCXML
    Sysml2Parser sysml_parser;
    FsmIr ir3;
    ASSERT_TRUE(sysml_parser.parse(sysml_str, ir3, err));
    const std::string scxml_str = ScxmlSerializer::serialize(ir3);

    // Hop 4: SCXML -> JSON
    ScxmlParser scxml_parser;
    FsmIr ir4;
    ASSERT_TRUE(scxml_parser.parse(scxml_str, ir4, err));
    const std::string json_str = JsonSerializer::serialize(ir4);

    // Hop 5: JSON -> DOT
    JsonStateParser json_parser;
    FsmIr ir5;
    ASSERT_TRUE(json_parser.parse(json_str, ir5, err));
    const std::string dot_str = DotSerializer::serialize(ir5);

    // Hop 6: DOT -> PlantUML
    DotParser dot_parser;
    FsmIr ir6;
    ASSERT_TRUE(dot_parser.parse(dot_str, ir6, err));
    const std::string puml_final = PlantUmlSerializer::serialize(ir6);

    // Hop 7: Final PlantUML Verification
    FsmIr ir_final;
    ASSERT_TRUE(puml_parser.parse(puml_final, ir_final, err));
    EXPECT_EQ(ir_final.states.size(), 4U);
    EXPECT_EQ(ir_final.transitions.size(), 6U);
}

/**
 * @brief Test Intent: Verify lossless preservation of native EFSM variables, signals, requirements, and lifecycle
 * actions.
 *
 * Scenario:
 * - Model with state variables, typed signals, traceability reqs, entry/do/exit actions, and deferred events.
 * - Test roundtrips to SysML v2, SCXML, JSON, and PlantUML.
 * - Verify all metadata attributes remain intact.
 */
TEST(LosslessRoundtripTest, NativeLanguageRoundtripAllProperties) {
    const std::string sysml_in = R"(
    state def SatelliteMission {
        attribute battery_percent : Integer = 100;
        attribute altitude_m : Real = 500;

        item def EvTelemetry {
            attribute battery_mv : Integer;
            attribute gps_locked : Boolean;
        }

        entry; then Standby;

        state Standby {
            satisfy requirement REQ_SAT_01;
            entry action InitSensors;
            do action background_telemetry;
            exit action Cleanup;
            defer EvTelemetry;
        }

        state Active {
            entry action ArmMotors;
        }

        transition from Standby accept StartCmd do StartEngines then Active;
    }
    )";

    std::string err;
    Sysml2Parser sysml_parser;
    FsmIr model;
    ASSERT_TRUE(sysml_parser.parse(sysml_in, model, err)) << "Error: " << err;

    EXPECT_EQ(model.variables.size(), 2u);
    EXPECT_EQ(model.signals.size(), 2u);
    EXPECT_EQ(model.states.size(), 2u);

    // 1. Serialize SysML v2 and reparse
    const std::string sysml_out = Sysml2Serializer::serialize(model);
    FsmIr sysml_roundtrip;
    ASSERT_TRUE(sysml_parser.parse(sysml_out, sysml_roundtrip, err)) << "SysML v2 roundtrip error: " << err;
    EXPECT_EQ(sysml_roundtrip.variables.size(), 2u);
    EXPECT_EQ(sysml_roundtrip.signals.size(), 2u);
    EXPECT_EQ(sysml_roundtrip.states.size(), 2u);
    const auto* st_sysml = sysml_roundtrip.find_state("Standby");
    ASSERT_NE(st_sysml, nullptr);
    EXPECT_EQ(st_sysml->entry_actions.size(), 1u);
    EXPECT_EQ(st_sysml->entry_actions[0].name, "InitSensors");
    EXPECT_EQ(st_sysml->do_activity, "background_telemetry");
    EXPECT_EQ(st_sysml->exit_actions.size(), 1u);
    EXPECT_EQ(st_sysml->exit_actions[0].name, "Cleanup");
    EXPECT_EQ(st_sysml->traceability_reqs.size(), 1u);
    EXPECT_EQ(st_sysml->traceability_reqs[0], "REQ_SAT_01");
    EXPECT_EQ(st_sysml->deferred_events.size(), 1u);

    // 2. Serialize SCXML and reparse
    const std::string scxml_out = ScxmlSerializer::serialize(model);
    ScxmlParser scxml_parser;
    FsmIr scxml_roundtrip;
    ASSERT_TRUE(scxml_parser.parse(scxml_out, scxml_roundtrip, err)) << "SCXML roundtrip error: " << err;
    EXPECT_EQ(scxml_roundtrip.variables.size(), 2u);
    EXPECT_EQ(scxml_roundtrip.states.size(), 2u);
    const auto* st_scxml = scxml_roundtrip.find_state("Standby");
    ASSERT_NE(st_scxml, nullptr);
    EXPECT_EQ(st_scxml->entry_actions.size(), 1u);
    EXPECT_EQ(st_scxml->exit_actions.size(), 1u);
    EXPECT_EQ(st_scxml->deferred_events.size(), 1u);

    // 3. Serialize JSON and reparse
    const std::string json_out = JsonSerializer::serialize(model);
    JsonStateParser json_parser;
    FsmIr json_roundtrip;
    ASSERT_TRUE(json_parser.parse(json_out, json_roundtrip, err)) << "JSON roundtrip error: " << err;
    EXPECT_EQ(json_roundtrip.variables.size(), 2u);
    EXPECT_EQ(json_roundtrip.signals.size(), 2u);
    EXPECT_EQ(json_roundtrip.states.size(), 2u);
    const auto* st_json = json_roundtrip.find_state("Standby");
    ASSERT_NE(st_json, nullptr);
    EXPECT_EQ(st_json->entry_actions.size(), 1u);
    EXPECT_EQ(st_json->do_activity, "background_telemetry");
    EXPECT_EQ(st_json->exit_actions.size(), 1u);
    EXPECT_EQ(st_json->traceability_reqs.size(), 1u);
    EXPECT_EQ(st_json->deferred_events.size(), 1u);

    // 4. Serialize PlantUML and reparse
    const std::string puml_out = PlantUmlSerializer::serialize(model);
    PlantUmlParser puml_parser;
    FsmIr puml_roundtrip;
    ASSERT_TRUE(puml_parser.parse(puml_out, puml_roundtrip, err)) << "PlantUML roundtrip error: " << err;
    EXPECT_EQ(puml_roundtrip.variables.size(), 2u);
    EXPECT_EQ(puml_roundtrip.signals.size(), 2u);
    EXPECT_EQ(puml_roundtrip.states.size(), 2u);
    const auto* st_puml = puml_roundtrip.find_state("Standby");
    ASSERT_NE(st_puml, nullptr);
    EXPECT_EQ(st_puml->entry_actions.size(), 1u);
    EXPECT_EQ(st_puml->do_activity, "background_telemetry");
    EXPECT_EQ(st_puml->exit_actions.size(), 1u);
    EXPECT_EQ(st_puml->deferred_events.size(), 1u);
}

TEST(LosslessRoundtripTest, Sysml2ToPlantUmlRoundtripWithDirectives) {
    const std::string sysml_input = R"(
state def SatelliteControl {
    attribute battery_soc : Real [percent] = 100;
    attribute velocity : Real [mm/s] = 0;

    entry; then Idle;

    state Idle {
        entry action init_sensors();
        do action send_heartbeat();
    }

    transition t1
        first Idle
        accept EvLaunch
        do action ignite_thrusters()
        then Active;

    state Active;
}
)";

    Sysml2Parser sysml_parser;
    FsmIr sysml_ir;
    std::string err;
    ASSERT_TRUE(sysml_parser.parse(sysml_input, sysml_ir, err)) << "SysML parse error: " << err;

    EXPECT_EQ(sysml_ir.variables.size(), 2u);
    EXPECT_EQ(sysml_ir.variables[0].name, "battery_soc");
    EXPECT_EQ(sysml_ir.variables[0].physical_unit, "percent");
    EXPECT_EQ(sysml_ir.variables[1].name, "velocity");
    EXPECT_EQ(sysml_ir.variables[1].physical_unit, "mm/s");

    // Serialize to PlantUML
    std::string puml_str = PlantUmlSerializer::serialize(sysml_ir);
    EXPECT_NE(puml_str.find("@fsm:var name=battery_soc"), std::string::npos);
    EXPECT_NE(puml_str.find("unit=\"percent\""), std::string::npos);
    EXPECT_NE(puml_str.find("unit=\"mm/s\""), std::string::npos);

    // Reparse from PlantUML
    PlantUmlParser puml_parser;
    FsmIr puml_ir;
    ASSERT_TRUE(puml_parser.parse(puml_str, puml_ir, err)) << "PlantUML parse error: " << err;

    EXPECT_EQ(puml_ir.variables.size(), 2u);
    EXPECT_EQ(puml_ir.variables[0].name, "battery_soc");
    EXPECT_EQ(puml_ir.variables[0].physical_unit, "percent");
    EXPECT_EQ(puml_ir.variables[1].name, "velocity");
    EXPECT_EQ(puml_ir.variables[1].physical_unit, "mm/s");

    const auto* idle_st = puml_ir.find_state("Idle");
    ASSERT_NE(idle_st, nullptr);
    EXPECT_EQ(idle_st->entry_actions.size(), 1u);
    EXPECT_EQ(idle_st->entry_actions[0].name, "init_sensors");
    EXPECT_EQ(idle_st->do_activity, "send_heartbeat");
}

TEST(LosslessRoundtripTest, ComplexHierarchicalSysml2AndSmvClosedLoop) {
    const std::string sysml_input = R"(
state def SatelliteSafety {
    attribute batterySoC : Real = 100.0;
    attribute sunPointed : Boolean = true;

    entry; then Booting;

    state Booting {
        transition boot_success
            first Booting
            accept BootCmd
            then Operational;
    }

    state Operational {
        state Standby;
        state Science;

        transition start_science
            first Standby
            accept StartScienceCmd
            then Science;
    }

    state SafeMode {
        state Diagnostic;
    }

    transition anomaly
        first Operational
        accept AnomalySignal
        then SafeMode;
}
)";

    Sysml2Parser sysml_parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(sysml_parser.parse(sysml_input, model, err)) << "Error: " << err;

    // Verify correct state hierarchy
    const auto* booting = model.find_state("Booting");
    ASSERT_NE(booting, nullptr);
    EXPECT_TRUE(booting->parent_state.empty());

    const auto* operational = model.find_state("Operational");
    ASSERT_NE(operational, nullptr);
    EXPECT_TRUE(operational->parent_state.empty());

    const auto* standby = model.find_state("Standby");
    ASSERT_NE(standby, nullptr);
    EXPECT_EQ(standby->parent_state, "Operational");

    const auto* safe = model.find_state("SafeMode");
    ASSERT_NE(safe, nullptr);
    EXPECT_TRUE(safe->parent_state.empty());

    // Serialize to SMV and reparse
    const std::string smv = SmvSerializer::serialize(model);
    SmvParser smv_parser;
    FsmIr smv_ir;
    ASSERT_TRUE(smv_parser.parse(smv, smv_ir, err)) << "SMV parse error: " << err;
    EXPECT_EQ(smv_ir.states.size(), model.states.size());
    EXPECT_EQ(smv_ir.events.size(), model.events.size());

    // Serialize SMV to Mermaid and reparse
    const std::string mmd = MermaidSerializer::serialize(smv_ir);
    MermaidParser mmd_parser;
    FsmIr mmd_ir;
    ASSERT_TRUE(mmd_parser.parse(mmd, mmd_ir, err)) << "Mermaid parse error: " << err;
    EXPECT_EQ(mmd_ir.states.size(), model.states.size());
}

/**
 * @brief Test Intent: Verify lossless roundtrip of Typed In/Out Ports and Numeric Assert Constraints across
 *                      SysML v2, PlantUML, SCXML, JSON, and SMV models.
 */
TEST(LosslessRoundtripTest, TypedPortsAndContractsRoundtrip) {
    constexpr const char* kSysMLv2PortsModel = R"(
package SpacecraftSubsystem {
    state def PowerManager {
        in port solar_flux : Real { assert constraint { self >= 0.0 and self <= 1400.0 } }
        out port bus_voltage : Real { assert constraint { self >= 24.0 and self <= 32.0 } }
        attribute battery_soc : Real = 100.0;

        entry; then Nominal;

        state Nominal {
            transition on_eclipse
                if solar_flux < 50.0
                then Eclipse;
        }

        state Eclipse;
    }
}
)";

    Sysml2Parser sysml_parser;
    FsmIr ir_sysml;
    std::string err;
    ASSERT_TRUE(sysml_parser.parse(kSysMLv2PortsModel, ir_sysml, err)) << err;

    ASSERT_EQ(ir_sysml.ports.size(), 2u);
    const auto* in_p = ir_sysml.find_port("solar_flux");
    ASSERT_NE(in_p, nullptr);
    EXPECT_TRUE(in_p->is_in());
    EXPECT_DOUBLE_EQ(in_p->min_value.value_or(0.0), 0.0);
    EXPECT_DOUBLE_EQ(in_p->max_value.value_or(0.0), 1400.0);

    const auto* out_p = ir_sysml.find_port("bus_voltage");
    ASSERT_NE(out_p, nullptr);
    EXPECT_TRUE(out_p->is_out());
    EXPECT_DOUBLE_EQ(out_p->min_value.value_or(0.0), 24.0);
    EXPECT_DOUBLE_EQ(out_p->max_value.value_or(0.0), 32.0);

    // 1. SysML v2 -> JSON -> parse JSON -> verify ports
    std::string json_str = JsonSerializer::serialize(ir_sysml);
    JsonStateParser json_parser;
    FsmIr ir_json;
    ASSERT_TRUE(json_parser.parse(json_str, ir_json, err)) << err;
    ASSERT_EQ(ir_json.ports.size(), 2u);
    const auto* in_json = ir_json.find_port("solar_flux");
    ASSERT_NE(in_json, nullptr);
    EXPECT_TRUE(in_json->is_in());
    EXPECT_DOUBLE_EQ(in_json->min_value.value_or(0.0), 0.0);
    EXPECT_DOUBLE_EQ(in_json->max_value.value_or(0.0), 1400.0);

    // 2. JSON -> PlantUML (@fsm:port) -> parse PlantUML -> verify ports
    std::string puml_str = PlantUmlSerializer::serialize(ir_json);
    PlantUmlParser puml_parser;
    FsmIr ir_puml;
    ASSERT_TRUE(puml_parser.parse(puml_str, ir_puml, err)) << err;
    ASSERT_EQ(ir_puml.ports.size(), 2u);
    const auto* in_puml = ir_puml.find_port("solar_flux");
    ASSERT_NE(in_puml, nullptr);
    EXPECT_TRUE(in_puml->is_in());
    EXPECT_DOUBLE_EQ(in_puml->min_value.value_or(0.0), 0.0);
    EXPECT_DOUBLE_EQ(in_puml->max_value.value_or(0.0), 1400.0);
}

/**
 * @brief Test Intent: Verify 100% lossless multi-format roundtrip and traceability requirements for Autonomous UAV
 * Mission preset.
 */
TEST(LosslessRoundtripTest, AutonomousUavMissionPreset) {
    constexpr const char* kUavSysml = R"(
state def AutonomousUavMission {
    in port battery_percent : Real { assert constraint { self >= 0.0 and self <= 100.0; } }
    in port altitude_m : Real { assert constraint { self >= 0.0 and self <= 10000.0; } }
    in port gps_locked : Boolean;
    out port motor_armed : Boolean;
    out port camera_active : Boolean;

    attribute waypoints_completed : Integer = 0;
    attribute retry_count : Integer = 0;

    event def CalibrationOk;
    event def TakeoffCmd;
    event def TargetAltitudeReached;
    event def AreaReached;
    event def NextSectorCmd;
    event def PauseCmd;
    event def ResumeMissionCmd;
    event def LowBatteryEvent;
    event def AbortCmd;
    event def HomePointReached;
    event def TouchdownEvent;
    event def ShutdownCmd;

    entry; then Preflight;

    state Preflight {
        satisfy requirement REQ_UAV_PRE_01;
        entry; then SensorCalib;
        state SensorCalib;
        state SystemReady;

        transition calib_done
            first SensorCalib
            accept CalibrationOk
            do action { out.motor_armed = true; }
            then SystemReady;
    }

    state InFlight {
        satisfy requirement REQ_UAV_NAV_01;
        do action async_flight_stabilizer;

        entry; then Ascending;
        state Ascending;

        state Navigating {
            defer EvTelemetryPing;
            defer EvCameraSnap;

            entry; then WaypointNav;
            state WaypointNav;
            state SearchPattern;

            transition area_reached
                first WaypointNav
                accept AreaReached
                do action { out.camera_active = true; }
                then SearchPattern;

            transition next_sector
                first SearchPattern
                accept NextSectorCmd
                do action { reg.waypoints_completed = reg.waypoints_completed + 1; }
                then WaypointNav;
        }

        state HoverPause {
            satisfy requirement REQ_UAV_HOLD_01;
        }

        transition alt_reached
            first Ascending
            accept TargetAltitudeReached
            do StartNavigation
            then Navigating;

        transition pause_mission
            first Navigating
            accept PauseCmd
            do HoldPosition
            then HoverPause;

        transition resume_mission
            first HoverPause
            accept ResumeMissionCmd
            do ResumeNavigation
            then Navigating[H];
    }

    state FailSafe {
        satisfy requirement REQ_UAV_SAFE_01;
        entry; then ReturnToHome;
        state ReturnToHome;
        state SafeLanding;
        state Landed;

        transition home_reached
            first ReturnToHome
            accept HomePointReached
            do DescendMotors
            then SafeLanding;

        transition touch_down
            first SafeLanding
            accept TouchdownEvent
            do action { out.motor_armed = false; }
            then Landed;
    }

    state Terminal;

    transition takeoff
        first Preflight
        accept TakeoffCmd
        if in.gps_locked and in.battery_percent > 20.0
        do LaunchUav
        then InFlight;

    transition low_bat
        first InFlight
        if in.battery_percent < 15.0
        do InitiateFailSafe
        then FailSafe;

    transition abort_flight
        first InFlight
        accept AbortCmd
        do InitiateFailSafe
        then FailSafe;

    transition shutdown
        first FailSafe
        accept ShutdownCmd
        do PowerOff
        then Terminal;
}
)";

    Sysml2Parser sysml_parser;
    FsmIr sysml_ir;
    std::string err;
    ASSERT_TRUE(sysml_parser.parse(kUavSysml, sysml_ir, err)) << err;

    EXPECT_EQ(sysml_ir.name, "AutonomousUavMission");
    EXPECT_EQ(sysml_ir.ports.size(), 5u);
    EXPECT_EQ(sysml_ir.variables.size(), 2u);

    // Verify traceability reqs in SysML IR
    const auto* pre_st = sysml_ir.find_state_by_name("Preflight");
    ASSERT_NE(pre_st, nullptr);
    ASSERT_EQ(pre_st->traceability_reqs.size(), 1u);
    EXPECT_EQ(pre_st->traceability_reqs[0], "REQ_UAV_PRE_01");

    const auto* nav_st = sysml_ir.find_state_by_name("InFlight");
    ASSERT_NE(nav_st, nullptr);
    ASSERT_EQ(nav_st->traceability_reqs.size(), 1u);
    EXPECT_EQ(nav_st->traceability_reqs[0], "REQ_UAV_NAV_01");

    auto verify_uav_model = [&](const FsmIr& ir, const std::string& fmt, bool check_reqs, bool check_ports_vars) {
        if (!ir.name.empty() && ir.name != "GeneratedFSM") {
            EXPECT_EQ(ir.name, "AutonomousUavMission") << "[" << fmt << "] Model name mismatch";
        }
        if (check_ports_vars) {
            EXPECT_EQ(ir.ports.size(), 5u) << "[" << fmt << "] Port count mismatch";
            EXPECT_EQ(ir.variables.size(), 2u) << "[" << fmt << "] Variable count mismatch";
        }
        if (check_reqs) {
            const auto* p_pre = ir.find_state_by_name("Preflight");
            ASSERT_NE(p_pre, nullptr) << "[" << fmt << "] Missing Preflight state";
            ASSERT_EQ(p_pre->traceability_reqs.size(), 1u) << "[" << fmt << "] Preflight missing req";
            EXPECT_EQ(p_pre->traceability_reqs[0], "REQ_UAV_PRE_01") << "[" << fmt << "]";

            const auto* p_nav = ir.find_state_by_name("InFlight");
            ASSERT_NE(p_nav, nullptr) << "[" << fmt << "] Missing InFlight state";
            ASSERT_EQ(p_nav->traceability_reqs.size(), 1u) << "[" << fmt << "] InFlight missing req";
            EXPECT_EQ(p_nav->traceability_reqs[0], "REQ_UAV_NAV_01") << "[" << fmt << "]";

            const auto* p_hold = ir.find_state_by_name("HoverPause");
            ASSERT_NE(p_hold, nullptr) << "[" << fmt << "] Missing HoverPause state";
            ASSERT_EQ(p_hold->traceability_reqs.size(), 1u) << "[" << fmt << "] HoverPause missing req";
            EXPECT_EQ(p_hold->traceability_reqs[0], "REQ_UAV_HOLD_01") << "[" << fmt << "]";

            const auto* p_safe = ir.find_state_by_name("FailSafe");
            ASSERT_NE(p_safe, nullptr) << "[" << fmt << "] Missing FailSafe state";
            ASSERT_EQ(p_safe->traceability_reqs.size(), 1u) << "[" << fmt << "] FailSafe missing req";
            EXPECT_EQ(p_safe->traceability_reqs[0], "REQ_UAV_SAFE_01") << "[" << fmt << "]";
        }
    };

    // 1. PlantUML Fine-Grained Roundtrip
    std::string puml = PlantUmlSerializer::serialize(sysml_ir);
    PlantUmlParser puml_parser;
    FsmIr puml_ir;
    ASSERT_TRUE(puml_parser.parse(puml, puml_ir, err)) << "[PlantUML] " << err;
    verify_uav_model(puml_ir, "PlantUML", true, true);

    // 2. SysML v2 Fine-Grained Roundtrip
    std::string sysml_out = Sysml2Serializer::serialize(sysml_ir);
    Sysml2Parser sysml_re_parser;
    FsmIr sysml_re_ir;
    ASSERT_TRUE(sysml_re_parser.parse(sysml_out, sysml_re_ir, err)) << "[SysML v2] " << err;
    verify_uav_model(sysml_re_ir, "SysML v2", true, true);

    // 3. XState JSON Fine-Grained Roundtrip
    std::string json_out = JsonSerializer::serialize(sysml_ir);
    JsonStateParser json_parser;
    FsmIr json_ir;
    ASSERT_TRUE(json_parser.parse(json_out, json_ir, err)) << "[JSON] " << err;
    verify_uav_model(json_ir, "JSON", true, true);

    // 4. W3C SCXML Fine-Grained Roundtrip
    std::string scxml_out = ScxmlSerializer::serialize(sysml_ir);
    ScxmlParser scxml_parser;
    FsmIr scxml_ir;
    ASSERT_TRUE(scxml_parser.parse(scxml_out, scxml_ir, err)) << "[SCXML] " << err;
    verify_uav_model(scxml_ir, "SCXML", true, true);
}

/**
 * @brief Test Intent: Verify universal lossless roundtrip of enum and struct definitions
 * across all 7 supported diagram and schema formats (Mermaid, PlantUML, SysML v2, JSON, DOT, SCXML, Cameo XMI).
 */
TEST(LosslessRoundtripTest, UniversalDataDefinitionsRoundtripAcrossAllFormats) {
    FsmIr baseline;
    baseline.name = "DataDefsFsm";
    baseline.initial_state = "Idle";

    StateNode s_idle;
    s_idle.name = "Idle";
    baseline.states.push_back(s_idle);

    StateNode s_active;
    s_active.name = "Active";
    baseline.states.push_back(s_active);

    TransitionEdge t1;
    t1.source = "Idle";
    t1.target = "Active";
    t1.event = "EvStart";
    baseline.transitions.push_back(t1);

    EnumDefinition en("FlightMode", "uint8_t", "UAV flight modes");
    en.add_literal("Manual", 0);
    en.add_literal("Auto", 1);
    en.add_literal("Failsafe", 2);
    baseline.add_enum(en);

    StructDefinition st("Waypoint", true, "3D Waypoint definition");
    st.add_field(StructField("latitude", "float", "0.0"));
    st.add_field(StructField("longitude", "float", "0.0"));
    st.add_field(StructField("altitude", "uint32_t", "100"));
    baseline.add_struct(st);

    verify_roundtrip(baseline);
}

}  // namespace


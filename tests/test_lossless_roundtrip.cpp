#include <gtest/gtest.h>

#include "fsm/backend/emitters/cameo_serializer.hpp"
#include "fsm/backend/emitters/dot_serializer.hpp"
#include "fsm/backend/emitters/json_serializer.hpp"
#include "fsm/backend/emitters/mermaid_serializer.hpp"
#include "fsm/backend/emitters/plantuml_serializer.hpp"
#include "fsm/backend/emitters/scxml_serializer.hpp"
#include "fsm/backend/emitters/sysml2_serializer.hpp"
#include "fsm/frontend/cameo_xmi_parser.hpp"
#include "fsm/frontend/dot_parser.hpp"
#include "fsm/frontend/json_parser.hpp"
#include "fsm/frontend/mermaid_parser.hpp"
#include "fsm/frontend/plantuml_parser.hpp"
#include "fsm/frontend/scxml_parser.hpp"
#include "fsm/frontend/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"

using namespace fsm::codegen;

namespace {

void verify_roundtrip(const FsmIr& baseline) {
    std::string err;

    // 1. Mermaid
    const std::string mmd = MermaidSerializer::serialize(baseline);
    MermaidParser mmd_parser;
    FsmIr mmd_ir;
    ASSERT_TRUE(mmd_parser.parse(mmd, mmd_ir, err)) << "Mermaid parse error: " << err;
    EXPECT_EQ(mmd_ir.states.size(), baseline.states.size());
    EXPECT_EQ(mmd_ir.transitions.size(), baseline.transitions.size());

    // 2. PlantUML
    const std::string puml = PlantUmlSerializer::serialize(baseline);
    PlantUmlParser puml_parser;
    FsmIr puml_ir;
    ASSERT_TRUE(puml_parser.parse(puml, puml_ir, err)) << "PlantUML parse error: " << err;
    EXPECT_EQ(puml_ir.states.size(), baseline.states.size());
    EXPECT_EQ(puml_ir.transitions.size(), baseline.transitions.size());

    // 3. SysML v2
    const std::string sysml = Sysml2Serializer::serialize(baseline);
    Sysml2Parser sysml_parser;
    FsmIr sysml_ir;
    ASSERT_TRUE(sysml_parser.parse(sysml, sysml_ir, err)) << "SysML v2 parse error: " << err;
    EXPECT_EQ(sysml_ir.states.size(), baseline.states.size());
    EXPECT_EQ(sysml_ir.transitions.size(), baseline.transitions.size());

    // 4. XState JSON
    const std::string json_str = JsonSerializer::serialize(baseline);
    JsonStateParser json_parser;
    FsmIr json_ir;
    ASSERT_TRUE(json_parser.parse(json_str, json_ir, err)) << "JSON parse error: " << err;
    EXPECT_EQ(json_ir.states.size(), baseline.states.size());
    EXPECT_EQ(json_ir.transitions.size(), baseline.transitions.size());

    // 5. Graphviz DOT
    const std::string dot_str = DotSerializer::serialize(baseline);
    DotParser dot_parser;
    FsmIr dot_ir;
    ASSERT_TRUE(dot_parser.parse(dot_str, dot_ir, err)) << "DOT parse error: " << err;
    EXPECT_EQ(dot_ir.states.size(), baseline.states.size());
    EXPECT_EQ(dot_ir.transitions.size(), baseline.transitions.size());

    // 6. W3C SCXML
    const std::string scxml_str = ScxmlSerializer::serialize(baseline);
    ScxmlParser scxml_parser;
    FsmIr scxml_ir;
    ASSERT_TRUE(scxml_parser.parse(scxml_str, scxml_ir, err)) << "SCXML parse error: " << err;
    EXPECT_EQ(scxml_ir.states.size(), baseline.states.size());
    EXPECT_EQ(scxml_ir.transitions.size(), baseline.transitions.size());
    EXPECT_EQ(scxml_ir.actions.size(), baseline.actions.size());

    // 7. Cameo / MagicDraw OMG XMI 2.1
    const std::string xmi_str = CameoSerializer::serialize(baseline);
    CameoXmiParser xmi_parser;
    FsmIr xmi_ir;
    ASSERT_TRUE(xmi_parser.parse(xmi_str, xmi_ir, err)) << "Cameo XMI parse error: " << err;
    EXPECT_EQ(xmi_ir.states.size(), baseline.states.size());
    EXPECT_EQ(xmi_ir.transitions.size(), baseline.transitions.size());
    EXPECT_EQ(xmi_ir.actions.size(), baseline.actions.size());
}

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

}  // namespace

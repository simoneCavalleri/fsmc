#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/emitters/json_serializer.hpp"
#include "fsm/backend/emitters/mermaid_serializer.hpp"
#include "fsm/backend/emitters/plantuml_serializer.hpp"
#include "fsm/backend/emitters/sysml2_serializer.hpp"
#include "fsm/frontend/cameo_xmi_parser.hpp"
#include "fsm/frontend/json_parser.hpp"
#include "fsm/frontend/mermaid_parser.hpp"
#include "fsm/frontend/plantuml_parser.hpp"
#include "fsm/frontend/scxml_parser.hpp"
#include "fsm/frontend/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::codegen;

namespace {

TEST(FormatExportTest, CameoToMermaidExport) {
    const std::string xmi = R"(<?xml version="1.0" encoding="UTF-8"?>
    <xmi:XMI xmi:version="2.1" xmlns:uml="http://www.omg.org/spec/UML/20090901" xmlns:xmi="http://schema.omg.org/spec/XMI/2.1">
      <uml:Model xmi:id="_m1" name="CameoExportModel">
        <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="ExportSM">
          <region xmi:id="_r1">
            <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps1" kind="initial"/>
            <subvertex xmi:type="uml:State" xmi:id="_s_idle" name="Idle"/>
            <subvertex xmi:type="uml:State" xmi:id="_s_running" name="Running"/>
            <transition xmi:id="_t0" source="_ps1" target="_s_idle"/>
            <transition xmi:id="_t1" source="_s_idle" target="_s_running">
              <trigger xmi:id="_tr1" name="StartCmd"/>
              <guard xmi:id="_g1" name="IsReadyGuard"/>
              <effect xmi:id="_act1" name="StartAction"/>
            </transition>
          </region>
        </packagedElement>
      </uml:Model>
    </xmi:XMI>)";

    CameoXmiParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, model, err)) << "Error: " << err;

    // Export to Mermaid
    const std::string mermaid_diagram = MermaidSerializer::serialize(model);
    EXPECT_NE(mermaid_diagram.find("stateDiagram-v2"), std::string::npos);
    EXPECT_NE(mermaid_diagram.find("[*] --> Idle"), std::string::npos);
    EXPECT_NE(mermaid_diagram.find("Idle --> Running : StartCmd [IsReadyGuard] / StartAction"), std::string::npos);

    // Verify the exported Mermaid can be re-parsed by MermaidParser
    MermaidParser mermaid_parser;
    FsmIr roundtrip_model;
    ASSERT_TRUE(mermaid_parser.parse(mermaid_diagram, roundtrip_model, err)) << "Error: " << err;
    ASSERT_NE(roundtrip_model.find_state("Idle"), nullptr);
    ASSERT_NE(roundtrip_model.find_state("Running"), nullptr);
    ASSERT_EQ(roundtrip_model.transitions.size(), 1U);
    EXPECT_EQ(roundtrip_model.transitions[0].event, "StartCmd");
    EXPECT_EQ(roundtrip_model.transitions[0].guard, "IsReadyGuard");
}

TEST(FormatExportTest, ScxmlToPlantUmlExport) {
    const std::string scxml = R"(<?xml version="1.0" encoding="UTF-8"?>
    <scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Disconnected" name="ConnectionFSM">
      <state id="Disconnected">
        <transition event="ConnectCmd" cond="HasNetworkGuard" target="Connecting">
          <send event="InitSocketAction"/>
        </transition>
      </state>
      <state id="Connecting"/>
    </scxml>)";

    ScxmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(scxml, model, err)) << "Error: " << err;

    // Export to PlantUML
    const std::string puml_diagram = PlantUmlSerializer::serialize(model);
    EXPECT_NE(puml_diagram.find("@startuml"), std::string::npos);
    EXPECT_NE(puml_diagram.find("[*] --> Disconnected"), std::string::npos);
    EXPECT_NE(puml_diagram.find("Disconnected --> Connecting : ConnectCmd [HasNetworkGuard] / InitSocketAction"),
              std::string::npos);
    EXPECT_NE(puml_diagram.find("@enduml"), std::string::npos);

    // Verify PlantUML roundtrip parse
    PlantUmlParser puml_parser;
    FsmIr roundtrip_model;
    ASSERT_TRUE(puml_parser.parse(puml_diagram, roundtrip_model, err)) << "Error: " << err;
    ASSERT_NE(roundtrip_model.find_state("Disconnected"), nullptr);
    ASSERT_NE(roundtrip_model.find_state("Connecting"), nullptr);
}

TEST(FormatExportTest, Sysml2Export) {
    FsmIr model;
    model.name = "ExportTest";
    model.initial_state = "Off";
    model.add_state("Off");
    model.add_state("On");

    TransitionEdge trans;
    trans.source = "Off";
    trans.target = "On";
    trans.event = "ToggleCmd";
    trans.guard = "PowerGuard";
    trans.action = "TurnOnAction";
    model.add_transition(trans);

    const std::string sysml = Sysml2Serializer::serialize(model);
    EXPECT_NE(sysml.find("state def ExportTest {"), std::string::npos);
    EXPECT_NE(sysml.find("initial state Off;"), std::string::npos);
    EXPECT_NE(sysml.find("transition from Off accept ToggleCmd if PowerGuard do TurnOnAction then On;"),
              std::string::npos);
}

TEST(FormatExportTest, IndustrialPressRoundtripAcrossPlantUmlMermaidJson) {
    const std::string puml = R"(@startuml
[*] --> Idle

Idle --> Initializing : PowerOnCmd [SafetyOk && !EStop] / LogPowerOn
Idle --> Idle : PingEvent / Heartbeat

state Initializing {
    [*] --> SelfTest
    SelfTest --> Calibrating : SelfTestOkEvent / StoreDiagnostics
    SelfTest --> Faulted : SelfTestFailEvent [!Recoverable] / LogFault
    SelfTest : ProgressEvent / UpdateDisplay
    Calibrating --> Ready : CalibrationDoneEvent / SaveOffsets
    Calibrating : ProgressEvent / UpdateDisplay
}

Initializing --> Idle : AbortCmd / Cleanup
Ready --> Operating : StartCmd [ToolLoaded && OperatorPresent] / EngageDrive

state Operating {
    [*] --> Running

    state Running {
        [*] --> Manual
        Manual --> Auto : AutoModeCmd [ConfigValid] / SwitchToAuto
        Auto --> Manual : ManualModeCmd / SwitchToManual
        Auto : SensorTickEvent / UpdateFeedback
        Manual : JogCmd / MoveAxis
        Running : HeartbeatEvent / ResetWatchdog
    }

    Running --> Paused : PauseCmd / HoldPosition
    Paused --> Running : ResumeCmd [SafetyOk] / ReleaseHold
    Paused --> Idle : after_30000ms / AutoShutdown
    Running --> Faulted : EStopEvent / EmergencyBrake
}

Operating --> Paused : SuspendCmd / SaveContext
Paused --> Operating[H] : ResumeSessionCmd / RestoreLastActiveState
Faulted --> Operating[H*] : ResetAndResumeCmd [!CriticalFault] / DeepRestoreState
Faulted --> Idle : AckFaultCmd [!CriticalFault] / ClearFault
Faulted --> Faulted : DiagnosticPollEvent / LogDiagnostics
Operating --> Idle : StopCmd / DisengageDrive
@enduml)";

    PlantUmlParser puml_parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(puml_parser.parse(puml, model, err)) << "Error: " << err;

    // 1. Verify AST Hierarchy
    EXPECT_EQ(model.initial_state, "Idle");
    EXPECT_TRUE(model.find_state("Idle")->parent_state.empty());
    EXPECT_TRUE(model.find_state("Initializing")->is_composite);
    EXPECT_TRUE(model.find_state("Initializing")->parent_state.empty());
    EXPECT_EQ(model.find_state("SelfTest")->parent_state, "Initializing");
    EXPECT_EQ(model.find_state("Calibrating")->parent_state, "Initializing");
    EXPECT_TRUE(model.find_state("Ready")->parent_state.empty());
    EXPECT_TRUE(model.find_state("Faulted")->parent_state.empty());
    EXPECT_TRUE(model.find_state("Operating")->is_composite);
    EXPECT_TRUE(model.find_state("Operating")->parent_state.empty());
    EXPECT_TRUE(model.find_state("Running")->is_composite);
    EXPECT_EQ(model.find_state("Running")->parent_state, "Operating");
    EXPECT_EQ(model.find_state("Manual")->parent_state, "Running");
    EXPECT_EQ(model.find_state("Auto")->parent_state, "Running");

    // 2. Test PlantUML Roundtrip
    const std::string exported_puml = PlantUmlSerializer::serialize(model);
    EXPECT_NE(exported_puml.find("state Initializing {"), std::string::npos);
    EXPECT_NE(exported_puml.find("state Operating {"), std::string::npos);
    EXPECT_NE(exported_puml.find("state Running {"), std::string::npos);
    EXPECT_NE(exported_puml.find("[SafetyOk && !EStop]"), std::string::npos);
    EXPECT_EQ(exported_puml.find("fsm::and_"), std::string::npos);  // Clean diagram format
    FsmIr roundtrip_puml;
    ASSERT_TRUE(puml_parser.parse(exported_puml, roundtrip_puml, err)) << "Error: " << err;
    EXPECT_EQ(roundtrip_puml.initial_state, "Idle");
    EXPECT_EQ(roundtrip_puml.find_state("SelfTest")->parent_state, "Initializing");
    EXPECT_EQ(roundtrip_puml.find_state("Manual")->parent_state, "Running");

    // 3. Test Mermaid Roundtrip
    const std::string exported_mmd = MermaidSerializer::serialize(model);
    EXPECT_NE(exported_mmd.find("state Initializing {"), std::string::npos);
    EXPECT_NE(exported_mmd.find("state Operating {"), std::string::npos);
    EXPECT_NE(exported_mmd.find("state Running {"), std::string::npos);
    EXPECT_NE(exported_mmd.find("[SafetyOk && !EStop]"), std::string::npos);
    EXPECT_EQ(exported_mmd.find("fsm::and_"), std::string::npos);  // Clean diagram format
    MermaidParser mmd_parser;
    FsmIr roundtrip_mmd;
    ASSERT_TRUE(mmd_parser.parse(exported_mmd, roundtrip_mmd, err)) << "Error: " << err;
    EXPECT_EQ(roundtrip_mmd.find_state("SelfTest")->parent_state, "Initializing");
    EXPECT_EQ(roundtrip_mmd.find_state("Manual")->parent_state, "Running");

    // 4. Test JSON Roundtrip
    const std::string exported_json = JsonSerializer::serialize(model);
    EXPECT_NE(exported_json.find("\"Initializing\": {"), std::string::npos);
    EXPECT_NE(exported_json.find("\"Operating\": {"), std::string::npos);
    EXPECT_NE(exported_json.find("\"guard\": \"SafetyOk && !EStop\""), std::string::npos);
    EXPECT_NE(exported_json.find("\"action\": \"LogPowerOn\""), std::string::npos);
    EXPECT_EQ(exported_json.find("fsm::and_"), std::string::npos);  // Clean diagram format
    JsonStateParser json_parser;
    FsmIr roundtrip_json;
    ASSERT_TRUE(json_parser.parse(exported_json, roundtrip_json, err)) << "Error: " << err;
    EXPECT_EQ(roundtrip_json.find_state("SelfTest")->parent_state, "Initializing");
    EXPECT_EQ(roundtrip_json.find_state("Manual")->parent_state, "Running");
}

}  // namespace

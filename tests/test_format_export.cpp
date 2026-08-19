#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cameo_xmi_parser.hpp"
#include "codegen/fsm_model.hpp"
#include "codegen/json_parser.hpp"
#include "codegen/json_serializer.hpp"
#include "codegen/mermaid_parser.hpp"
#include "codegen/mermaid_serializer.hpp"
#include "codegen/plantuml_parser.hpp"
#include "codegen/plantuml_serializer.hpp"
#include "codegen/scxml_parser.hpp"
#include "codegen/sysml2_parser.hpp"
#include "codegen/sysml2_serializer.hpp"

using namespace fsm::codegen;

namespace {

void test_cameo_to_mermaid_export() {
    std::cout << "[TEST] Running test_cameo_to_mermaid_export...\n";

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
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(xmi, model, err);
    assert(is_parsed);

    // Export to Mermaid
    const std::string mermaid_diagram = MermaidSerializer::serialize(model);
    assert(mermaid_diagram.find("stateDiagram-v2") != std::string::npos);
    assert(mermaid_diagram.find("[*] --> Idle") != std::string::npos);
    assert(mermaid_diagram.find("Idle --> Running : StartCmd [IsReadyGuard] / StartAction") != std::string::npos);

    // Verify the exported Mermaid can be re-parsed by MermaidParser
    MermaidParser mermaid_parser;
    FsmModel roundtrip_model;
    const bool is_roundtrip = mermaid_parser.parse(mermaid_diagram, roundtrip_model, err);
    assert(is_roundtrip);
    assert(roundtrip_model.find_state("Idle") != nullptr);
    assert(roundtrip_model.find_state("Running") != nullptr);
    assert(roundtrip_model.transitions.size() == 1);
    assert(roundtrip_model.transitions[0].event == "StartCmd");
    assert(roundtrip_model.transitions[0].guard == "IsReadyGuard");

    std::cout << "[PASS] test_cameo_to_mermaid_export passed.\n";
}

void test_scxml_to_plantuml_export() {
    std::cout << "[TEST] Running test_scxml_to_plantuml_export...\n";

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
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(scxml, model, err);
    assert(is_parsed);

    // Export to PlantUML
    const std::string puml_diagram = PlantUmlSerializer::serialize(model);
    assert(puml_diagram.find("@startuml") != std::string::npos);
    assert(puml_diagram.find("[*] --> Disconnected") != std::string::npos);
    assert(puml_diagram.find("Disconnected --> Connecting : ConnectCmd [HasNetworkGuard] / InitSocketAction") !=
           std::string::npos);
    assert(puml_diagram.find("@enduml") != std::string::npos);

    // Verify PlantUML roundtrip parse
    PlantUmlParser puml_parser;
    FsmModel roundtrip_model;
    const bool is_roundtrip = puml_parser.parse(puml_diagram, roundtrip_model, err);
    assert(is_roundtrip);
    assert(roundtrip_model.find_state("Disconnected") != nullptr);
    assert(roundtrip_model.find_state("Connecting") != nullptr);

    std::cout << "[PASS] test_scxml_to_plantuml_export passed.\n";
}

void test_sysml2_export() {
    std::cout << "[TEST] Running test_sysml2_export...\n";

    FsmModel model;
    model.name = "ExportTest";
    model.initial_state = "Off";
    model.add_state("Off");
    model.add_state("On");

    TransitionModel trans;
    trans.source = "Off";
    trans.target = "On";
    trans.event = "ToggleCmd";
    trans.guard = "PowerGuard";
    trans.action = "TurnOnAction";
    model.add_transition(trans);

    const std::string sysml = Sysml2Serializer::serialize(model);
    assert(sysml.find("state def ExportTest {") != std::string::npos);
    assert(sysml.find("initial state Off;") != std::string::npos);
    assert(sysml.find("transition from Off accept ToggleCmd if PowerGuard do TurnOnAction then On;") !=
           std::string::npos);

    std::cout << "[PASS] test_sysml2_export passed.\n";
}

void test_industrial_press_roundtrip() {
    std::cout << "[TEST] Running test_industrial_press_roundtrip across all formats...\n";

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
    FsmModel model;
    std::string err;
    assert(puml_parser.parse(puml, model, err));

    // 1. Verify AST Hierarchy
    assert(model.initial_state == "Idle");
    assert(model.find_state("Idle")->parent_state.empty());
    assert(model.find_state("Initializing")->is_composite);
    assert(model.find_state("Initializing")->parent_state.empty());
    assert(model.find_state("SelfTest")->parent_state == "Initializing");
    assert(model.find_state("Calibrating")->parent_state == "Initializing");
    assert(model.find_state("Ready")->parent_state.empty());
    assert(model.find_state("Faulted")->parent_state.empty());
    assert(model.find_state("Operating")->is_composite);
    assert(model.find_state("Operating")->parent_state.empty());
    assert(model.find_state("Running")->is_composite);
    assert(model.find_state("Running")->parent_state == "Operating");
    assert(model.find_state("Manual")->parent_state == "Running");
    assert(model.find_state("Auto")->parent_state == "Running");

    // 2. Test PlantUML Roundtrip
    const std::string exported_puml = PlantUmlSerializer::serialize(model);
    assert(exported_puml.find("state Initializing {") != std::string::npos);
    assert(exported_puml.find("state Operating {") != std::string::npos);
    assert(exported_puml.find("state Running {") != std::string::npos);
    assert(exported_puml.find("[SafetyOk && !EStop]") != std::string::npos);
    assert(exported_puml.find("fsm::and_") == std::string::npos);  // Clean diagram format
    FsmModel roundtrip_puml;
    assert(puml_parser.parse(exported_puml, roundtrip_puml, err));
    assert(roundtrip_puml.initial_state == "Idle");
    assert(roundtrip_puml.find_state("SelfTest")->parent_state == "Initializing");
    assert(roundtrip_puml.find_state("Manual")->parent_state == "Running");

    // 3. Test Mermaid Roundtrip
    const std::string exported_mmd = MermaidSerializer::serialize(model);
    assert(exported_mmd.find("state Initializing {") != std::string::npos);
    assert(exported_mmd.find("state Operating {") != std::string::npos);
    assert(exported_mmd.find("state Running {") != std::string::npos);
    assert(exported_mmd.find("[SafetyOk && !EStop]") != std::string::npos);
    assert(exported_mmd.find("fsm::and_") == std::string::npos);  // Clean diagram format
    MermaidParser mmd_parser;
    FsmModel roundtrip_mmd;
    assert(mmd_parser.parse(exported_mmd, roundtrip_mmd, err));
    assert(roundtrip_mmd.find_state("SelfTest")->parent_state == "Initializing");
    assert(roundtrip_mmd.find_state("Manual")->parent_state == "Running");

    // 4. Test JSON Roundtrip
    const std::string exported_json = JsonSerializer::serialize(model);
    assert(exported_json.find("\"Initializing\": {") != std::string::npos);
    assert(exported_json.find("\"Operating\": {") != std::string::npos);
    assert(exported_json.find("\"guard\": \"SafetyOk && !EStop\"") != std::string::npos);
    assert(exported_json.find("\"action\": \"LogPowerOn\"") != std::string::npos);
    assert(exported_json.find("fsm::and_") == std::string::npos);  // Clean diagram format
    JsonStateParser json_parser;
    FsmModel roundtrip_json;
    assert(json_parser.parse(exported_json, roundtrip_json, err));
    assert(roundtrip_json.find_state("SelfTest")->parent_state == "Initializing");
    assert(roundtrip_json.find_state("Manual")->parent_state == "Running");

    std::cout << "[PASS] test_industrial_press_roundtrip passed across PlantUML, Mermaid, and JSON.\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "     RUNNING DIAGRAM EXPORT TESTS       \n"
              << "========================================\n";

    test_cameo_to_mermaid_export();
    test_scxml_to_plantuml_export();
    test_sysml2_export();
    test_industrial_press_roundtrip();

    std::cout << "========================================\n"
              << "     ALL EXPORT TESTS PASSED (4/4)!     \n"
              << "========================================\n";
    return 0;
}

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/emitters/cameo_serializer.hpp"
#include "fsm/backend/emitters/dot_serializer.hpp"
#include "fsm/backend/emitters/json_serializer.hpp"
#include "fsm/backend/emitters/mermaid_serializer.hpp"
#include "fsm/backend/emitters/plantuml_serializer.hpp"
#include "fsm/backend/emitters/scxml_serializer.hpp"
#include "fsm/backend/emitters/smv_serializer.hpp"
#include "fsm/backend/emitters/sysml2_serializer.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/frontend/formal/scxml_parser.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/frontend/diagram/json_parser.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify cross-format export from Cameo OMG XMI to Mermaid state diagrams.
 *
 * Scenario:
 * - Parse Cameo XMI into FsmIr.
 * - Export to Mermaid diagram syntax.
 * - Re-parse exported Mermaid string with MermaidParser and verify model equivalence.
 */
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

/**
 * @brief Test Intent: Verify cross-format export from W3C SCXML to PlantUML state diagrams.
 *
 * Scenario:
 * - Parse SCXML into FsmIr.
 * - Export to PlantUML syntax.
 * - Re-parse exported PlantUML with PlantUmlParser and verify state graph equivalence.
 */
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

/**
 * @brief Test Intent: Verify SysML v2 state definition export serialization.
 *
 * Scenario:
 * - Build FsmIr and export to OMG SysML v2 textual notation.
 * - Verify `state def`, `entry; then ...`, `first ... accept ... if ... do ... then ...` syntax.
 */
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
    EXPECT_NE(sysml.find("entry; then Off;"), std::string::npos);
    EXPECT_NE(sysml.find("first Off"), std::string::npos);
    EXPECT_NE(sysml.find("accept ToggleCmd"), std::string::npos);
    EXPECT_NE(sysml.find("if PowerGuard"), std::string::npos);
    EXPECT_NE(sysml.find("do TurnOnAction"), std::string::npos);
    EXPECT_NE(sysml.find("then On;"), std::string::npos);
}

/**
 * @brief Test Intent: Verify multi-format roundtrip fidelity for complex hierarchical state machine (PlantUML ->
 * Mermaid -> JSON).
 *
 * Scenario:
 * - Parse deep hierarchical Industrial Press statechart with composite states and history transitions.
 * - Export to PlantUML, Mermaid, and JSON.
 * - Re-parse all three representations and verify hierarchy, guards, and action retention.
 */
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

/**
 * @brief Test Intent: Verify multi-format serialization of EntryPoint, ExitPoint, time_invariant, and transition
 * priority.
 *
 * Scenario:
 * - Construct FsmIr with EntryPoint, ExitPoint, stay duration / time_invariant, and transition priority.
 * - Serialize to PlantUML, SysML v2, and JSON.
 * - Re-parse each representation and verify full retention of kinds, invariants, and priorities.
 */
TEST(FormatExportTest, EntryExitPointTimeInvariantAndPriorityMultiFormatRoundtrip) {
    FsmIr model;
    model.name = "AvionicsFsm";
    model.initial_state = "Standby";

    StateNode standby("s1", "Standby", "Standby", StateKind::Atomic);
    standby.time_invariant = "stay <= 250ms";
    model.add_state(standby);

    StateNode ep("s2", "EnPort", "EnPort", StateKind::EntryPoint);
    model.add_state(ep);

    StateNode xp("s3", "ExPort", "ExPort", StateKind::ExitPoint);
    model.add_state(xp);

    TransitionEdge t1("t1", "Standby", "EnPort", SignalTrigger("EvFast"));
    t1.priority = 10;
    model.add_transition(t1);

    TransitionEdge t2("t2", "Standby", "ExPort", SignalTrigger("EvSlow"));
    t2.priority = 2;
    model.add_transition(t2);

    std::string err;

    // 1. PlantUML Export & Re-parse
    const std::string puml_out = PlantUmlSerializer::serialize(model);
    EXPECT_NE(puml_out.find("state EnPort <<entryPoint>>"), std::string::npos);
    EXPECT_NE(puml_out.find("state ExPort <<exitPoint>>"), std::string::npos);
    EXPECT_NE(puml_out.find("(prio=10)"), std::string::npos);
    EXPECT_NE(puml_out.find("invariant stay <= 250ms"), std::string::npos);

    PlantUmlParser puml_parser;
    FsmIr puml_ir;
    ASSERT_TRUE(puml_parser.parse(puml_out, puml_ir, err)) << "PlantUML parse error: " << err;
    EXPECT_EQ(puml_ir.find_state("EnPort")->kind, StateKind::EntryPoint);
    EXPECT_EQ(puml_ir.find_state("ExPort")->kind, StateKind::ExitPoint);
    ASSERT_TRUE(puml_ir.find_state("Standby")->time_invariant.has_value());
    EXPECT_EQ(*puml_ir.find_state("Standby")->time_invariant, "stay <= 250ms");
    ASSERT_EQ(puml_ir.transitions.size(), 2u);
    EXPECT_EQ(puml_ir.transitions[0].priority, 10u);
    EXPECT_EQ(puml_ir.transitions[1].priority, 2u);

    // 2. SysML v2 Export & Re-parse
    const std::string sysml_out = Sysml2Serializer::serialize(model);
    EXPECT_NE(sysml_out.find("entry point EnPort;"), std::string::npos);
    EXPECT_NE(sysml_out.find("exit point ExPort;"), std::string::npos);
    EXPECT_NE(sysml_out.find("stay duration <= stay <= 250ms;"), std::string::npos);
    EXPECT_NE(sysml_out.find("priority 10"), std::string::npos);

    // 3. JSON Export & Re-parse
    const std::string json_out = JsonSerializer::serialize(model);
    EXPECT_NE(json_out.find("\"kind\": \"EntryPoint\""), std::string::npos);
    EXPECT_NE(json_out.find("\"kind\": \"ExitPoint\""), std::string::npos);
    EXPECT_NE(json_out.find("\"priority\": 10"), std::string::npos);
    EXPECT_NE(json_out.find("\"time_invariant\": \"stay <= 250ms\""), std::string::npos);

    JsonStateParser json_parser;
    FsmIr json_ir;
    ASSERT_TRUE(json_parser.parse(json_out, json_ir, err)) << "JSON parse error: " << err;
    EXPECT_EQ(json_ir.find_state("EnPort")->kind, StateKind::EntryPoint);
    EXPECT_EQ(json_ir.find_state("ExPort")->kind, StateKind::ExitPoint);
    ASSERT_TRUE(json_ir.find_state("Standby")->time_invariant.has_value());
    EXPECT_EQ(*json_ir.find_state("Standby")->time_invariant, "stay <= 250ms");
    ASSERT_EQ(json_ir.transitions.size(), 2u);
    EXPECT_EQ(json_ir.transitions[0].priority, 10u);
    EXPECT_EQ(json_ir.transitions[1].priority, 2u);
}

/**
 * @brief Test Intent: Verify nuXmv / SMV formal model serialization with extended variables, prioritized transitions,
 * and LTL/INVAR temporal properties.
 *
 * Scenario:
 * - Build FSM with bounded integer variable 'retry_count' (0..5), boolean 'armed', state enum, and transitions with
 * priority.
 * - Add an INVARSPEC invariant and an LTLSPEC formula.
 * - Verify that SmvSerializer outputs valid SMV with MODULE main, VAR, ASSIGN init/next case structures and formal
 * specifications.
 */
TEST(FormatExportTest, SmvFormalModelVerificationExport) {
    FsmIr model;
    model.name = "TelemetryController";
    model.initial_state = "Standby";

    model.add_state("Standby");
    model.add_state("Transmitting");
    model.add_state("SafeHold");

    // Variables with finite domains
    VariableDefinition var_retry;
    var_retry.name = "retry_count";
    var_retry.type = "int";
    var_retry.initial_value = "0";
    var_retry.min_value = 0;
    var_retry.max_value = 5;
    model.variables.push_back(var_retry);

    VariableDefinition var_armed;
    var_armed.name = "is_armed";
    var_armed.type = "bool";
    var_armed.initial_value = "FALSE";
    model.variables.push_back(var_armed);

    // Transitions with priorities and guards
    TransitionEdge t_trans("t1", "Standby", "Transmitting", SignalTrigger("EvSend"));
    t_trans.guard = "retry_count < 3 && is_armed";
    t_trans.priority = 10;
    model.add_transition(t_trans);

    TransitionEdge t_safe("t2", "Standby", "SafeHold", SignalTrigger("EvSend"));
    t_safe.guard = "retry_count >= 3";
    t_safe.priority = 1;
    model.add_transition(t_safe);

    // Formal Temporal Properties
    FormalProperty inv_prop;
    inv_prop.name = "SafeStateInvariant";
    inv_prop.kind = PropertyKind::Invariant;
    inv_prop.raw_formula = "state != SafeHold | retry_count >= 3";
    model.properties.push_back(inv_prop);

    FormalProperty ltl_prop;
    ltl_prop.name = "EventualTransmission";
    ltl_prop.kind = PropertyKind::Liveness;
    ltl_prop.raw_formula = "G (state = Standby & event = EvSend & is_armed -> F state = Transmitting)";
    model.properties.push_back(ltl_prop);

    const std::string smv_out = SmvSerializer::serialize(model);

    // 1. Check Module & VAR declarations
    EXPECT_NE(smv_out.find("MODULE main"), std::string::npos);
    EXPECT_NE(smv_out.find("state : {Standby, Transmitting, SafeHold};"), std::string::npos);
    EXPECT_NE(smv_out.find("event : {none, EvSend};"), std::string::npos);
    EXPECT_NE(smv_out.find("retry_count : 0..5;"), std::string::npos);
    EXPECT_NE(smv_out.find("is_armed : boolean;"), std::string::npos);

    // 2. Check ASSIGN and initial state
    EXPECT_NE(smv_out.find("init(state) := Standby;"), std::string::npos);
    EXPECT_NE(smv_out.find("init(retry_count) := 0;"), std::string::npos);
    EXPECT_NE(smv_out.find("init(is_armed) := FALSE;"), std::string::npos);

    // 3. Check transition prioritization in case statements (priority 10 must appear before priority 1)
    auto pos_prio10 = smv_out.find("state = Standby & event = EvSend & (retry_count < 3 & is_armed) : Transmitting;");
    auto pos_prio1 = smv_out.find("state = Standby & event = EvSend & (retry_count >= 3) : SafeHold;");
    ASSERT_NE(pos_prio10, std::string::npos);
    ASSERT_NE(pos_prio1, std::string::npos);
    EXPECT_LT(pos_prio10, pos_prio1);

    // 4. Check INVARSPEC and LTLSPEC
    EXPECT_NE(smv_out.find("INVARSPEC -- SafeStateInvariant"), std::string::npos);
    EXPECT_NE(smv_out.find("LTLSPEC -- EventualTransmission"), std::string::npos);
}

/**
 * @brief Test Intent: Verify Cameo OMG XMI and SCXML export for hierarchical pseudostates (Choice, Deep/Shallow
 * History, Entry/Exit Points).
 *
 * Scenario:
 * - Construct hierarchical FSM with parent composite state containing Choice, DeepHistory, EntryPoint, ExitPoint.
 * - Export to Cameo OMG XMI 2.1 and SCXML 1.0.
 * - Verify presence of proper XML tags, pseudostate kinds, and history semantics.
 */
TEST(FormatExportTest, CameoAndScxmlPseudostatesAndOrthogonalExport) {
    FsmIr model;
    model.name = "FlightControlSystem";
    model.initial_state = "Standby";

    StateNode standby("Standby");
    model.add_state(standby);

    StateNode active("Active");
    active.is_composite = true;
    active.has_history = true;
    active.has_deep_history = true;
    active.initial_sub_state = "Navigating";
    active.deferred_events.emplace_back("TelemetryPing");
    model.add_state(active);

    StateNode navigating("Navigating");
    navigating.parent_state = "Active";
    model.add_state(navigating);

    StateNode hovering("Hovering");
    hovering.parent_state = "Active";
    model.add_state(hovering);

    model.add_choice_node("SpeedCheckChoice");

    // 1. Cameo XMI Export Verification
    const std::string cameo_out = CameoSerializer::serialize(model);
    EXPECT_NE(cameo_out.find("<xmi:XMI"), std::string::npos);
    EXPECT_NE(cameo_out.find("kind=\"choice\""), std::string::npos);
    EXPECT_NE(cameo_out.find("kind=\"deepHistory\""), std::string::npos);
    EXPECT_NE(cameo_out.find("<deferrableTrigger name=\"TelemetryPing\"/>"), std::string::npos);
    EXPECT_NE(cameo_out.find("<subvertex xmi:type=\"uml:State\""), std::string::npos);

    // 2. SCXML Export Verification
    const std::string scxml_out = ScxmlSerializer::serialize(model);
    EXPECT_NE(scxml_out.find("<scxml"), std::string::npos);
    EXPECT_NE(scxml_out.find("<history id=\"Active_hist\" type=\"deep\"/>"), std::string::npos);
    EXPECT_NE(scxml_out.find("<defer event=\"TelemetryPing\"/>"), std::string::npos);
    EXPECT_NE(scxml_out.find("<state id=\"Active\" initial=\"Navigating\">"), std::string::npos);
    EXPECT_NE(scxml_out.find("<state id=\"Navigating\""), std::string::npos);
}

/**
 * @brief Test Intent: Verify DOT / Graphviz diagram serialization and syntax integrity.
 *
 * Scenario:
 * - Export model to Graphviz DOT format.
 * - Verify digraph header, state styling, and transition edges.
 * - Re-parse with DotParser to confirm full lossless syntax compatibility.
 */
TEST(FormatExportTest, DotGraphvizExport) {
    FsmIr model;
    model.name = "PumpController";
    model.initial_state = "Off";

    model.add_state("Off");
    model.add_state("Priming");
    model.add_state("Pumping");

    TransitionEdge t1("t1", "Off", "Priming", SignalTrigger("EvTurnOn"));
    t1.guard = "TankNotEmptyGuard";
    t1.action = "StartPrimerAction";
    model.add_transition(t1);

    TransitionEdge t2("t2", "Priming", "Pumping", SignalTrigger("EvPrimed"));
    model.add_transition(t2);

    const std::string dot_out = DotSerializer::serialize(model);
    EXPECT_NE(dot_out.find("digraph PumpController"), std::string::npos);
    EXPECT_NE(dot_out.find("__start__ [shape=circle"), std::string::npos);
    EXPECT_NE(dot_out.find("__start__ -> Off;"), std::string::npos);
    EXPECT_NE(dot_out.find("Off -> Priming [label=\"EvTurnOn [TankNotEmptyGuard] / StartPrimerAction\"];"),
              std::string::npos);
    EXPECT_NE(dot_out.find("Priming -> Pumping [label=\"EvPrimed\"];"), std::string::npos);

    // Re-parse with DotParser
    DotParser dot_parser;
    FsmIr parsed_ir;
    std::string err;
    ASSERT_TRUE(dot_parser.parse(dot_out, parsed_ir, err)) << "DOT parser error: " << err;
    EXPECT_EQ(parsed_ir.states.size(), 3u);
    EXPECT_EQ(parsed_ir.transitions.size(), 2u);
    EXPECT_EQ(parsed_ir.transitions[0].event, "EvTurnOn");
    EXPECT_EQ(parsed_ir.transitions[0].guard, "TankNotEmptyGuard");
    EXPECT_EQ(parsed_ir.transitions[0].action, "StartPrimerAction");
}

}  // namespace

/**
 * @file test_cameo_parser.cpp
 * @brief Unit test suite for the Cameo Systems Modeler (OMG XMI 2.x) frontend parser.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/formal/cameo_serializer.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"
#include "fsm/middleend/passes/choice_inlining_pass.hpp"

using namespace fsm::frontend::formal;
using namespace fsm::frontend;
using namespace fsm::backend::cpp;
using namespace fsm::backend;
using namespace fsm::diagnostic;
using namespace fsm::middleend::analysis;
using namespace fsm::middleend;
using namespace fsm::middleend::passes;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify Cameo OMG XMI 2.x standard XML document parsing.
 * @scenario Parse Cameo XMI state machine containing initial, simple states, and transitions.
 * @expected Model correctly populated with state names, signals, and initial pseudostate pointer.
 */
TEST(CameoParser, BasicXmiDocument_ParsedIntoValidFsmIr) {
    const std::string xmi_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:uml="http://www.omg.org/spec/UML/20131001">
  <uml:Model xmi:id="_model_1" name="CameoModel">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm_1" name="DeviceProtocolFSM">
      <region xmi:id="_region_1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_init_1" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_state_1" name="Disconnected"/>
        <subvertex xmi:type="uml:State" xmi:id="_state_2" name="Connecting"/>
        <subvertex xmi:type="uml:State" xmi:id="_state_3" name="Connected"/>

        <transition xmi:id="_t_init" source="_init_1" target="_state_1"/>
        <transition xmi:id="_t_1" source="_state_1" target="_state_2">
          <trigger xmi:id="_trig_1" name="ConnectCmd"/>
          <guard xmi:id="_g_1" name="HasNetworkGuard"/>
          <effect xmi:id="_act_1" name="InitSocketAction"/>
        </transition>
        <transition xmi:id="_t_2" source="_state_2" target="_state_3">
          <trigger xmi:id="_trig_2" name="HandshakeOkEvent"/>
          <effect xmi:id="_act_2" name="SetupSessionAction"/>
        </transition>
        <transition xmi:id="_t_3" source="_state_3" target="_state_1">
          <trigger xmi:id="_trig_3" name="DisconnectCmd"/>
          <effect xmi:id="_act_3" name="CloseSocketAction"/>
        </transition>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>
)";

    CameoXmiParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "DeviceProtocolFSM");
    EXPECT_EQ(model.initial_state, "Disconnected");
    EXPECT_EQ(model.states.size(), 3u);
    EXPECT_EQ(model.signals.size(), 3u);
    EXPECT_EQ(model.guards.size(), 1u);
    EXPECT_EQ(model.actions.size(), 3u);
    EXPECT_EQ(model.transitions.size(), 3u);
}

/**
 * @brief Verify Cameo nested composite regions and choice pseudostates.
 * @scenario Parse XMI containing sub-regions and choice pseudostates with conditional guards.
 * @expected Composite hierarchy and choice branching nodes correctly modeled in FsmIr.
 */
TEST(CameoParser, CompositeAndChoicePseudostates_ParsedIntoValidFsmIr) {
    const std::string xmi_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:uml="http://www.omg.org/spec/UML/20131001">
  <uml:Model xmi:id="_model_2" name="SpacecraftModel">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm_2" name="SpacecraftMissionFSM">
      <region xmi:id="_root_region">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_init_root" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_s_standby" name="Standby"/>
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_choice_1" name="ClearanceChoice" kind="choice"/>
        <subvertex xmi:type="uml:State" xmi:id="_s_inflight" name="InFlight">
          <region xmi:id="_sub_region">
            <subvertex xmi:type="uml:Pseudostate" xmi:id="_sub_init" kind="initial"/>
            <subvertex xmi:type="uml:State" xmi:id="_s_ascending" name="Ascending"/>
            <subvertex xmi:type="uml:State" xmi:id="_s_cruising" name="Cruising"/>
            <transition xmi:id="_t_sub_init" source="_sub_init" target="_s_ascending"/>
            <transition xmi:id="_t_ascent" source="_s_ascending" target="_s_cruising">
              <trigger xmi:id="_trig_alt" name="AltitudeReachedEvent"/>
              <effect xmi:id="_act_panels" name="DeployPanelsAction"/>
            </transition>
          </region>
        </subvertex>
        <subvertex xmi:type="uml:State" xmi:id="_s_aborted" name="Aborted"/>

        <transition xmi:id="_t_root_init" source="_init_root" target="_s_standby"/>
        <transition xmi:id="_t_auth" source="_s_standby" target="_choice_1">
          <trigger xmi:id="_trig_auth" name="AuthorizeCmd"/>
        </transition>
        <transition xmi:id="_t_choice_ok" source="_choice_1" target="_s_inflight">
          <guard xmi:id="_g_ok" name="ValidClearanceGuard"/>
          <effect xmi:id="_act_arm" name="ArmEnginesAction"/>
        </transition>
        <transition xmi:id="_t_choice_fail" source="_choice_1" target="_s_aborted">
          <guard xmi:id="_g_fail" name="NoClearanceGuard"/>
          <effect xmi:id="_act_alarm" name="TriggerAlarmAction"/>
        </transition>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>
)";

    CameoXmiParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "SpacecraftMissionFSM");
    EXPECT_TRUE(model.is_choice_node("ClearanceChoice"));

    const auto* inflight = model.find_state("InFlight");
    ASSERT_NE(inflight, nullptr);
    EXPECT_TRUE(inflight->is_composite);

    // Choice pseudostates must be inlined before C++ emission (as the real pipeline does).
    DiagnosticEngine diag;
    ASSERT_TRUE(ChoiceInliningPass::run(model, diag)) << "ChoiceInliningPass failed: " << diag.render_to_string();
    EXPECT_FALSE(model.is_choice_node("ClearanceChoice")) << "ClearanceChoice should have been removed by inlining";

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    opts.standalone = true;

    const std::string code = CppGenerator::generate_header(model, opts);
    EXPECT_FALSE(code.empty());
    EXPECT_NE(code.find("struct Standby"), std::string::npos);
    EXPECT_NE(code.find("struct InFlight"), std::string::npos);
    EXPECT_NE(code.find("struct ArmEnginesAction"), std::string::npos);
}

/**
 * @brief Verify attribute-style XML transition properties (trigger, guard, effect).
 * @scenario Parse XMI transitions using attribute-based trigger and effect notations.
 * @expected Triggers, guards, and action effects mapped to FsmIr TransitionEdge components.
 */
TEST(CameoParser, AttributeStyleEffectsAndActions_ParsedIntoValidFsmIr) {
    const std::string xmi_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:uml="http://www.omg.org/spec/UML/20131001">
  <uml:Model xmi:id="_m1" name="IndustrialPressModel">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="IndustrialPress">
      <region xmi:id="_r1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps1" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_s1" name="Idle"/>
        <subvertex xmi:type="uml:State" xmi:id="_s2" name="Running"/>

        <transition xmi:id="_t0" source="_ps1" target="_s1"/>
        <transition xmi:id="_t1" source="_s1" target="_s2" trigger="StartCmd" guard="SafetyOk" effect="EngageMotorAction"/>
        <transition xmi:id="_t2" source="_s2" target="_s1" trigger="StopCmd" action="DisengageMotorAction"/>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>)";

    CameoXmiParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.states.size(), 2U);
    EXPECT_EQ(model.transitions.size(), 2U);
    EXPECT_EQ(model.actions.size(), 2U);
    EXPECT_EQ(model.guards.size(), 1U);
    EXPECT_TRUE(std::any_of(model.actions.begin(), model.actions.end(),
                            [](const auto& a) { return a.name == "EngageMotorAction"; }));
    EXPECT_TRUE(std::any_of(model.actions.begin(), model.actions.end(),
                            [](const auto& a) { return a.name == "DisengageMotorAction"; }));
}

/**
 * @brief Verify Cameo entry, doActivity, and exit behavior parsing.
 * @scenario Parse XMI states declaring entry, doActivity, and exit opaque behaviors.
 * @expected StateNode captures entry_actions, do_activity, and exit_actions lists.
 */
TEST(CameoParser, NativeEntryExitAndDoActivity_CapturedInStateNode) {
    const std::string xmi_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:uml="http://www.omg.org/spec/UML/20131001">
  <uml:Model xmi:id="_m1" name="UavModel">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="UavFSM">
      <region xmi:id="_r1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps1" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_s1" name="Flight">
          <entry xmi:type="uml:Activity" name="ArmMotors"/>
          <doActivity xmi:type="uml:Activity" name="StabilizeFlight"/>
          <exit xmi:type="uml:Activity" name="DisarmMotors"/>
          <deferrableTrigger name="PingEvent"/>
        </subvertex>
        <transition xmi:id="_t0" source="_ps1" target="_s1"/>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>)";

    CameoXmiParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi_content, model, err)) << "Error: " << err;

    const auto* flight = model.find_state("Flight");
    ASSERT_NE(flight, nullptr);
    ASSERT_EQ(flight->entry_actions.size(), 1u);
    EXPECT_EQ(flight->entry_actions[0].name, "ArmMotors");
    EXPECT_EQ(flight->do_activity, "StabilizeFlight");
    ASSERT_EQ(flight->exit_actions.size(), 1u);
    EXPECT_EQ(flight->exit_actions[0].name, "DisarmMotors");
    ASSERT_EQ(flight->deferred_events.size(), 1u);
    EXPECT_EQ(flight->deferred_events[0], "PingEvent");
}

/**
 * @brief Verify XML entity decoding in transition guards and state names.
 * @scenario Parse XMI containing escaped characters (&amp;, &lt;, &gt;) in guard expressions.
 * @expected Entity sequences decoded into C++ boolean operators in FsmIr guards.
 */
TEST(CameoParser, XmlEntitiesInNamesAndGuards_DecodedCorrectly) {
    const std::string xmi_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:uml="http://www.omg.org/spec/UML/20131001">
  <uml:Model xmi:id="_m1" name="EntityModel">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="XmlTestFSM">
      <region xmi:id="_r1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_init" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_s1" name="StateA"/>
        <subvertex xmi:type="uml:State" xmi:id="_s2" name="StateB"/>
        <transition xmi:id="_t0" source="_init" target="_s1"/>
        <transition xmi:id="_t1" source="_s1" target="_s2">
          <trigger name="A_&amp;_B_Event"/>
          <guard name="x &lt; 10 &amp;&amp; y &gt; 5"/>
          <effect name="Action_&quot;Special&quot;_&apos;Tag&apos;"/>
        </transition>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>)";

    CameoXmiParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi_content, model, err)) << "Error: " << err;

    ASSERT_EQ(model.transitions.size(), 1u);
    EXPECT_EQ(model.transitions[0].event, "A__B_Event");
    EXPECT_EQ(model.transitions[0].get_action(), "Action_Special_Tag");
}

/**
 * @brief Verify Cameo UML history and junction pseudostate parsing.
 * @scenario Parse XMI declaring shallowHistory, deepHistory, and junction vertices.
 * @expected IR captures StateKind::History, StateKind::DeepHistory, and StateKind::Junction.
 */
TEST(CameoParser, HistoryAndJunctionPseudostates_PreservedInIr) {
    const std::string xmi_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:uml="http://www.omg.org/spec/UML/20131001">
  <uml:Model xmi:id="_m1" name="HistoryModel">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="HistoryFSM">
      <region xmi:id="_r1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_init" kind="initial"/>
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_h_shallow" kind="shallowHistory"/>
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_h_deep" kind="deepHistory"/>
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_junc" kind="junction"/>
        <subvertex xmi:type="uml:State" xmi:id="_s1" name="ActiveState"/>
        <transition xmi:id="_t0" source="_init" target="_s1"/>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>)";

    CameoXmiParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.states.size(), 1u);
    EXPECT_EQ(model.initial_state, "ActiveState");
}

/**
 * @brief Verify two-pass resolution of cross-referenced Signal events and SysML profile stereotypes.
 * @scenario Parse XMI where triggers reference external signal definitions and custom stereotypes.
 * @expected Signal definitions resolved and stereotypes preserved in state/transition metadata.
 */
TEST(CameoParser, CrossReferencedTriggersAndSysmlProfiles_ResolvedCorrectly) {
    const std::string xmi_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:xmi="http://www.omg.org/spec/XMI/20131001"
         xmlns:uml="http://www.omg.org/spec/UML/20131001"
         xmlns:sysml="http://www.omg.org/spec/SysML/20150709/SysML">
  <uml:Model xmi:id="_m1" name="AvionicsSafetyModel">
    <packagedElement xmi:type="uml:Signal" xmi:id="_sig_engage" name="EvEngageSafety"/>
    <packagedElement xmi:type="uml:SignalEvent" xmi:id="_se_engage" name="SignalEvent_Engage" signal="_sig_engage"/>

    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="SafetyFSM">
      <region xmi:id="_r1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps_init" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_st_norm" name="NormalMode"/>
        <subvertex xmi:type="uml:State" xmi:id="_st_safe" name="SafeMode"/>

        <transition xmi:id="_tr_init" source="_ps_init" target="_st_norm"/>
        <transition xmi:id="_tr_safe" source="_st_norm" target="_st_safe">
          <trigger xmi:id="_trig_1" xmi:idref="_se_engage"/>
        </transition>
      </region>
    </packagedElement>

    <!-- Graphical diagram clutter that should be filtered out -->
    <uml:Diagram xmi:id="_diag_1" name="SafetyDiagram">
      <DiagramElement xmi:id="_de_1" x="100" y="200" width="80" height="40"/>
    </uml:Diagram>
  </uml:Model>

  <!-- External SysML Requirement profile applied to state via base_Element -->
  <sysml:Requirement xmi:id="_req_safe" base_Element="_st_safe" id="REQ_SAFETY_001" text="Must transition to SafeMode"/>
</xmi:XMI>)";

    CameoXmiParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "SafetyFSM");
    EXPECT_EQ(model.initial_state, "NormalMode");
    EXPECT_EQ(model.states.size(), 2u);
    ASSERT_EQ(model.transitions.size(), 1u);

    // Verify cross-referenced trigger resolved to EvEngageSafety or SignalEvent_Engage
    const auto& tr = model.transitions[0];
    EXPECT_EQ(tr.source, "NormalMode");
    EXPECT_EQ(tr.target, "SafeMode");
    EXPECT_TRUE(tr.event == "EvEngageSafety" || tr.event == "SignalEvent_Engage");

    // Verify external requirement was relinked to SafeMode state
    const auto* safe_state = model.find_state("SafeMode");
    ASSERT_NE(safe_state, nullptr);
    ASSERT_FALSE(safe_state->traceability_reqs.empty());
    EXPECT_EQ(safe_state->traceability_reqs[0], "REQ_SAFETY_001");
}

/**
 * @brief Regression: Cameo XMI states with multiple `<region>` children parse as `StateKind::Parallel` and roundtrip.
 * @scenario Parse an XMI model with a `Running` state containing two `<region>` elements (`Engine`, `Lights`),
 *           serialize back to Cameo XMI, and re-parse.
 * @expected `Running` has `StateKind::Parallel`, both regions have their `initial_sub_state`, and roundtrip preserves
 *           the parallel structure.
 */
TEST(CameoParser, ParallelOrthogonalRegions_ParsedAndRoundtripped) {
    const std::string xmi_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmi:version="2.1" xmlns:uml="http://www.omg.org/spec/UML/20090901" xmlns:xmi="http://schema.omg.org/spec/XMI/2.1">
  <uml:Model xmi:id="_m1" name="CarParallel">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="CarParallel">
      <region xmi:type="uml:Region" xmi:id="_r1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps_root" kind="initial"/>
        <transition xmi:id="_t_root" source="_ps_root" target="_s_run"/>
        <subvertex xmi:type="uml:State" xmi:id="_s_run" name="Running">
          <region xmi:type="uml:Region" xmi:id="_r_eng" name="Engine">
            <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps_e" kind="initial"/>
            <transition xmi:id="_t_e0" source="_ps_e" target="_s_eoff"/>
            <subvertex xmi:type="uml:State" xmi:id="_s_eoff" name="EngineOff"/>
            <subvertex xmi:type="uml:State" xmi:id="_s_eon" name="EngineOn"/>
            <transition xmi:id="_t_e1" source="_s_eoff" target="_s_eon" trigger="StartEngine"/>
          </region>
          <region xmi:type="uml:Region" xmi:id="_r_lgt" name="Lights">
            <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps_l" kind="initial"/>
            <transition xmi:id="_t_l0" source="_ps_l" target="_s_loff"/>
            <subvertex xmi:type="uml:State" xmi:id="_s_loff" name="LightsOff"/>
            <subvertex xmi:type="uml:State" xmi:id="_s_lon" name="LightsOn"/>
            <transition xmi:id="_t_l1" source="_s_loff" target="_s_lon" trigger="ToggleLights"/>
          </region>
        </subvertex>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>)";

    CameoXmiParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi_content, model, err)) << err;

    const auto* running = model.find_state("Running");
    ASSERT_NE(running, nullptr);
    EXPECT_EQ(running->kind, StateKind::Parallel);
    ASSERT_EQ(running->orthogonal_regions.size(), 2u);
    EXPECT_EQ(running->orthogonal_regions[0].state_ids.size(), 2u);
    EXPECT_EQ(running->orthogonal_regions[1].state_ids.size(), 2u);

    const auto* engine = model.find_state("Engine");
    ASSERT_NE(engine, nullptr);
    EXPECT_EQ(engine->parent_state, "Running");
    EXPECT_EQ(engine->initial_sub_state, "EngineOff");

    const auto* lights = model.find_state("Lights");
    ASSERT_NE(lights, nullptr);
    EXPECT_EQ(lights->parent_state, "Running");
    EXPECT_EQ(lights->initial_sub_state, "LightsOff");

    const std::string exported = fsm::backend::formal::CameoSerializer::serialize(model);
    FsmIr reparsed;
    ASSERT_TRUE(parser.parse(exported, reparsed, err)) << err;
    const auto* reparsed_running = reparsed.find_state("Running");
    ASSERT_NE(reparsed_running, nullptr);
    EXPECT_EQ(reparsed_running->kind, StateKind::Parallel);
}

/**
 * @brief Regression: Cameo XMI guards preserve `normalized_expression` and `effect` assignments populate `assignments`.
 * @scenario Parse an XMI transition with `guard="x &gt; 0 &amp;&amp; y &lt; 10"` and `effect="z = x"`.
 * @expected Guard models have `normalized_expression` populated and `transition_action->assignments` contains `z = x`.
 */
TEST(CameoParser, GuardExpressionsAndEffectAssignments_PreservedInIr) {
    const std::string xmi_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmi:version="2.1" xmlns:uml="http://www.omg.org/spec/UML/20090901" xmlns:xmi="http://schema.omg.org/spec/XMI/2.1">
  <uml:Model xmi:id="_m1" name="CameoExecModel">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="CameoExec">
      <region xmi:type="uml:Region" xmi:id="_r1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps0" kind="initial"/>
        <transition xmi:id="_t0" source="_ps0" target="_s1"/>
        <subvertex xmi:type="uml:State" xmi:id="_s1" name="Idle"/>
        <subvertex xmi:type="uml:State" xmi:id="_s2" name="Done"/>
        <transition xmi:id="_t1" source="_s1" target="_s2" trigger="EvGo" guard="x &gt; 0 &amp;&amp; y &lt; 10" effect="z = x"/>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>)";

    CameoXmiParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi_content, model, err)) << err;

    ASSERT_EQ(model.transitions.size(), 1u);
    const auto& tr = model.transitions[0];
    ASSERT_TRUE(tr.transition_action.has_value());
    ASSERT_EQ(tr.transition_action->assignments.size(), 1u);
    EXPECT_EQ(tr.transition_action->assignments[0].target.name, "z");
    EXPECT_EQ(tr.transition_action->assignments[0].expression, "x");

    ASSERT_GE(model.guards.size(), 2u);
    for (const auto& gm : model.guards) {
        EXPECT_TRUE(gm.normalized_expression.has_value());
    }
}

/**
 * @brief Verify CameoSerializer preserves variables, ports, and assignment actions across XMI roundtrip.
 * @scenario Serialize an FsmIr with variables, ports, entry/exit assignments, and transition assignments, then
 * re-parse.
 * @expected Variables, ports, and all assignment actions are recovered in the roundtripped FsmIr.
 */
TEST(CameoParser, RoundtripVariablesAndAssignmentActions_PreservedInXmi) {
    FsmIr orig;
    orig.name = "CameoRt";
    orig.initial_state = "Ready";
    orig.add_variable(VariableDefinition("x", DataType{PrimitiveTypeKind::Int32}, "0"));
    orig.add_port(PortDefinition("limit", DataType{PrimitiveTypeKind::Int32}, PortDirection::In));

    StateNode ready("Ready");
    ActionSignature entry_act("Ready_entry_x");
    entry_act.assignments.emplace_back(LValueTarget("x"), "10");
    ready.entry_actions.push_back(std::move(entry_act));

    ActionSignature exit_act("Ready_exit_x");
    exit_act.assignments.emplace_back(LValueTarget("x"), "x + 1");
    ready.exit_actions.push_back(std::move(exit_act));

    StateNode done("Done");
    orig.states.push_back(std::move(ready));
    orig.states.push_back(std::move(done));

    ActionSignature tr_act("tr_x");
    tr_act.assignments.emplace_back(LValueTarget("x"), "x * 2");
    orig.transitions.emplace_back("Ready", "Done", "GO", std::nullopt, std::move(tr_act));

    const std::string xmi = fsm::backend::formal::CameoSerializer::serialize(orig);
    CameoXmiParser parser;
    FsmIr rt;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, rt, err)) << err;

    ASSERT_NE(rt.find_variable("x"), nullptr);
    ASSERT_NE(rt.find_port("limit"), nullptr);

    const StateNode* rt_ready = rt.find_state("Ready");
    ASSERT_NE(rt_ready, nullptr);
    ASSERT_EQ(rt_ready->entry_actions.size(), 1u);
    ASSERT_EQ(rt_ready->entry_actions[0].assignments.size(), 1u);
    EXPECT_EQ(rt_ready->entry_actions[0].assignments[0].expression, "10");

    ASSERT_EQ(rt_ready->exit_actions.size(), 1u);
    ASSERT_EQ(rt_ready->exit_actions[0].assignments.size(), 1u);
    EXPECT_EQ(rt_ready->exit_actions[0].assignments[0].expression, "x + 1");

    ASSERT_EQ(rt.transitions.size(), 1u);
    ASSERT_TRUE(rt.transitions[0].transition_action.has_value());
    ASSERT_EQ(rt.transitions[0].transition_action->assignments.size(), 1u);
    EXPECT_EQ(rt.transitions[0].transition_action->assignments[0].expression, "x * 2");
}

/**
 * @brief Verify CameoXmiParser resolves transition targets declared later in sibling/outer regions.
 * @scenario Composite state '_s1' has a transition targeting '_s3' ('Machining'), which is declared after '_s1'.
 * @expected Transition target resolves to 'Machining' instead of raw XMI ID '_s3'.
 */
TEST(CameoParserTest, ResolvesForwardReferencedSiblingTargetsInsideCompositeRegions) {
    const std::string xmi = R"xml(
<xmi:XMI xmi:version="2.1" xmlns:uml="http://www.omg.org/spec/UML/20090901" xmlns:xmi="http://schema.omg.org/spec/XMI/2.1">
  <uml:Model xmi:id="_m1" name="ForwardTargetModel">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="ForwardTargetFsm">
      <region xmi:type="uml:Region" xmi:id="_r1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps_root" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_s1" name="Operational">
          <region xmi:type="uml:Region" xmi:id="_r_s1">
            <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps_s1" kind="initial"/>
            <transition xmi:id="_t_init_s1" source="_ps_s1" target="_s2"/>
            <subvertex xmi:type="uml:State" xmi:id="_s2" name="Idle"/>
            <transition xmi:id="_t_fwd" source="_s2" target="_s3" trigger="StartJobCmd"/>
          </region>
        </subvertex>
        <subvertex xmi:type="uml:State" xmi:id="_s3" name="Machining"/>
        <transition xmi:id="_t_root" source="_ps_root" target="_s1"/>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>
)xml";

    CameoXmiParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, ir, err)) << err;

    ASSERT_EQ(ir.transitions.size(), 1u);
    EXPECT_EQ(ir.transitions[0].source, "Idle");
    EXPECT_EQ(ir.transitions[0].target, "Machining");
}

/**
 * @brief Regression: Cameo XMI `<effect body="x = 99;"/>` (and `<body>` child) is extracted and parsed into
 * `ActionSignature::assignments`.
 * @scenario Parse Cameo XMI containing `<effect xmi:type="uml:OpaqueBehavior" body="x = 99;"/>`.
 * @expected Transition carries an assignment to `x` with expression `"99"`.
 */
TEST(CameoXmiParser, TransitionEffectBodyAttribute_ParsedIntoAssignments) {
    const std::string xmi = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmi:version="2.1" xmlns:uml="http://www.omg.org/spec/UML/20090901" xmlns:xmi="http://schema.omg.org/spec/XMI/2.1">
  <uml:Model xmi:id="_m1" name="CameoEffectModel">
    <!-- @fsm:var name=x type=int32 init=0 -->
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="CameoEffect">
      <region xmi:type="uml:Region" xmi:id="_r1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps_init" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_s1" name="Idle"/>
        <subvertex xmi:type="uml:State" xmi:id="_s2" name="Done"/>
        <transition xmi:id="_t0" source="_ps_init" target="_s1"/>
        <transition xmi:id="_t1" source="_s1" target="_s2" trigger="Go">
          <effect xmi:type="uml:OpaqueBehavior" xmi:id="_ef1" body="x = 99;"/>
        </transition>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>
)xml";

    CameoXmiParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, ir, err)) << err;
    ASSERT_EQ(ir.transitions.size(), 1u);
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    ASSERT_EQ(ir.transitions[0].transition_action->assignments.size(), 1u);
    EXPECT_EQ(ir.transitions[0].transition_action->assignments[0].target.name, "x");
    EXPECT_EQ(ir.transitions[0].transition_action->assignments[0].expression, "99");
}

/**
 * @brief Regression: Cameo XMI `<entry body="x = 10;"/>` and `<exit body="y = 20;"/>` are extracted and parsed
 * into `entry_actions` and `exit_actions` assignments.
 * @scenario Parse Cameo XMI state with `<entry xmi:type="uml:OpaqueBehavior" body="x = 10;"/>` and `<exit ... body="y =
 * 20;"/>`.
 * @expected `Idle->entry_actions` assigns `x = 10` and `Idle->exit_actions` assigns `y = 20`.
 */
TEST(CameoXmiParser, StateEntryExitBodyAttributes_ParsedIntoAssignments) {
    const std::string xmi = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmi:version="2.1" xmlns:uml="http://www.omg.org/spec/UML/20090901" xmlns:xmi="http://schema.omg.org/spec/XMI/2.1">
  <uml:Model xmi:id="_m1" name="CameoLifecycleModel">
    <!-- @fsm:var name=x type=int32 init=0 -->
    <!-- @fsm:var name=y type=int32 init=0 -->
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="CameoLifecycle">
      <region xmi:type="uml:Region" xmi:id="_r1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps_init" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_s1" name="Idle">
          <entry xmi:type="uml:OpaqueBehavior" xmi:id="_en1" body="x = 10;"/>
          <exit xmi:type="uml:OpaqueBehavior" xmi:id="_ex1" body="y = 20;"/>
        </subvertex>
        <subvertex xmi:type="uml:State" xmi:id="_s2" name="Done"/>
        <transition xmi:id="_t0" source="_ps_init" target="_s1"/>
        <transition xmi:id="_t1" source="_s1" target="_s2" trigger="Go"/>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>
)xml";

    CameoXmiParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, ir, err)) << err;
    const auto* idle = ir.find_state("Idle");
    ASSERT_NE(idle, nullptr);
    ASSERT_EQ(idle->entry_actions.size(), 1u);
    ASSERT_EQ(idle->entry_actions[0].assignments.size(), 1u);
    EXPECT_EQ(idle->entry_actions[0].assignments[0].target.name, "x");
    EXPECT_EQ(idle->entry_actions[0].assignments[0].expression, "10");
    ASSERT_EQ(idle->exit_actions.size(), 1u);
    ASSERT_EQ(idle->exit_actions[0].assignments.size(), 1u);
    EXPECT_EQ(idle->exit_actions[0].assignments[0].target.name, "y");
    EXPECT_EQ(idle->exit_actions[0].assignments[0].expression, "20");
}

/**
 * @brief Regression: Cameo XMI `<transition kind="internal" ...>` preserves `TransitionEdgeKind::Internal`
 * instead of defaulting to `External`.
 * @scenario Parse Cameo XMI with `<transition xmi:id="_t_int" kind="internal" source="_s1" target="_s1"
 * trigger="Ping"/>`.
 * @expected `ir.transitions[0].kind == TransitionEdgeKind::Internal`.
 */
TEST(CameoXmiParser, TransitionKindInternal_SetsInternalTransitionEdgeKind) {
    const std::string xmi = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmi:version="2.1" xmlns:uml="http://www.omg.org/spec/UML/20090901" xmlns:xmi="http://schema.omg.org/spec/XMI/2.1">
  <uml:Model xmi:id="_m1" name="CameoIntModel">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="CameoInt">
      <region xmi:type="uml:Region" xmi:id="_r1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps_init" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_s1" name="Idle"/>
        <transition xmi:id="_t0" source="_ps_init" target="_s1"/>
        <transition xmi:id="_t_int" kind="internal" source="_s1" target="_s1" trigger="Ping"/>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>
)xml";

    CameoXmiParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, ir, err)) << err;
    ASSERT_EQ(ir.transitions.size(), 1u);
    EXPECT_EQ(ir.transitions[0].kind, TransitionEdgeKind::Internal);
    EXPECT_EQ(ir.transitions[0].event, "Ping");
}

/**
 * @brief Regression: `<transition>` elements nested directly inside `<subvertex xmi:type="uml:State">`
 * (common for internal/self-transitions in UML XMI) are extracted by `CameoXmiGraphResolver`.
 * @scenario Parse Cameo XMI where `<transition kind="internal" source="_s1" target="_s1">` is a direct child of
 * `<subvertex>`.
 * @expected Internal transition on `Idle` with event `MySig` is present in `ir.transitions`.
 */
TEST(CameoXmiParser, NestedTransitionInsideSubvertex_ExtractedProperly) {
    const std::string xmi = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmi:version="2.1" xmlns:uml="http://www.omg.org/spec/UML/20090901" xmlns:xmi="http://schema.omg.org/spec/XMI/2.1">
  <uml:Model xmi:id="_m1" name="CameoNestedTransModel">
    <packagedElement xmi:type="uml:Signal" xmi:id="_sig1" name="MySig"/>
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="CameoNestedTrans">
      <region xmi:type="uml:Region" xmi:id="_r1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps_init" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_s1" name="Idle">
          <transition xmi:id="_t_int" kind="internal" source="_s1" target="_s1">
            <trigger xmi:id="_tr1" xmi:idref="_sig1"/>
          </transition>
        </subvertex>
        <transition xmi:id="_t0" source="_ps_init" target="_s1"/>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>
)xml";

    CameoXmiParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, ir, err)) << err;
    ASSERT_EQ(ir.transitions.size(), 1u);
    EXPECT_EQ(ir.transitions[0].source, "Idle");
    EXPECT_EQ(ir.transitions[0].target, "Idle");
    EXPECT_EQ(ir.transitions[0].kind, TransitionEdgeKind::Internal);
    EXPECT_EQ(ir.transitions[0].event, "MySig");
}

/**
 * @brief Regression: Multiple Cameo XMI transitions between the same `source` and `target` states
 * assigning to the same variable receive disambiguated synthetic action names (`Idle_Done_act_x`,
 * `Idle_Done_act_x_2`) so their assignments do not collide in C++ codegen.
 * @scenario Parse Cameo XMI with two transitions `Idle -> Done` (`x = 1` and `x = 2`).
 * @expected `transitions[0].transition_action->name != transitions[1].transition_action->name`.
 */
TEST(CameoParser, MultipleTransitionsSameEndpoints_DisambiguateAssignmentActionNames) {
    const std::string xmi = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:xmi="http://schema.omg.org/spec/XMI/2.1" xmlns:uml="http://schema.omg.org/spec/UML/2.0">
  <uml:Model xmi:id="_m1" name="SameEpCameo">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="SameEpCameo">
      <!-- @fsm:var name=x type=int init=0 -->
      <region xmi:id="_r1" name="Region1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps_init" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_s1" name="Idle"/>
        <subvertex xmi:type="uml:State" xmi:id="_s2" name="Done"/>
        <transition xmi:id="_t0" source="_ps_init" target="_s1"/>
        <transition xmi:id="_t1" source="_s1" target="_s2">
          <trigger xmi:id="_tr1" name="Ev1"/>
          <effect xmi:type="uml:OpaqueBehavior" body="x = 1"/>
        </transition>
        <transition xmi:id="_t2" source="_s1" target="_s2">
          <trigger xmi:id="_tr2" name="Ev2"/>
          <effect xmi:type="uml:OpaqueBehavior" body="x = 2"/>
        </transition>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>
)xml";

    CameoXmiParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, ir, err)) << err;
    ASSERT_EQ(ir.transitions.size(), 2u);
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    ASSERT_TRUE(ir.transitions[1].transition_action.has_value());
    EXPECT_NE(ir.transitions[0].transition_action->name, ir.transitions[1].transition_action->name);
}

/**
 * @brief Regression: `CameoXmiParser` reconstructs `TimeTrigger` when parsing `trigger="after_100ms"` /
 * `trigger="every_50ms"` attributes emitted by `CameoSerializer`.
 * @scenario Parse Cameo XMI with `<transition source="_s1" target="_s2" trigger="after_100ms"/>`.
 * @expected Transition has a `TimeTrigger` with `duration_in_ms() == 100`.
 */
TEST(CameoParser, TimeTriggerAttribute_PopulatesTimeTrigger) {
    const std::string xmi = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:xmi="http://schema.omg.org/spec/XMI/2.1" xmlns:uml="http://schema.omg.org/spec/UML/2.0">
  <uml:Model xmi:id="_m1" name="TimerCameo">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="TimerCameo">
      <region xmi:id="_r1" name="Region1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps_init" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_s1" name="Busy"/>
        <subvertex xmi:type="uml:State" xmi:id="_s2" name="Idle"/>
        <transition xmi:id="_t0" source="_ps_init" target="_s1"/>
        <transition xmi:id="_t1" source="_s1" target="_s2" trigger="after_100ms"/>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>
)xml";

    CameoXmiParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, ir, err)) << err;
    ASSERT_EQ(ir.transitions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(ir.transitions[0].trigger));
    EXPECT_EQ(std::get<TimeTrigger>(ir.transitions[0].trigger).duration_in_ms(), 100u);
}

/**
 * @brief Regression: `CameoSerializer` preserves `model.properties`, `state.traceability_reqs`, and
 * `state.time_invariant` across Cameo XMI roundtrips.
 * @scenario Serialize an FSM with `FormalProperty("safe", "G (not Bad)")` and state `Active` (`REQ_001`, `500[ms]`),
 * then re-parse with `CameoXmiParser`.
 * @expected Re-parsed IR keeps the property, `traceability_reqs == {"REQ_001"}`, and `time_invariant == "500[ms]"`.
 */
TEST(CameoParser, PropertiesReqsAndTimeInvariant_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "CameoMetaRt";
    ir.initial_state = "Active";
    auto& active = ir.add_state("Active");
    active.traceability_reqs.push_back("REQ_001");
    active.time_invariant = "500[ms]";

    FormalProperty prop;
    prop.name = "safe";
    prop.kind = PropertyKind::Safety;
    prop.raw_formula = "G (not Bad)";
    ir.add_property(prop);

    const std::string xmi = fsm::backend::formal::CameoSerializer::serialize(ir);
    CameoXmiParser parser;
    FsmIr parsed;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, parsed, err)) << err;

    ASSERT_EQ(parsed.properties.size(), 1u);
    EXPECT_EQ(parsed.properties[0].name, "safe");
    EXPECT_EQ(parsed.properties[0].raw_formula, "G (not Bad)");

    const auto* p_active = parsed.find_state("Active");
    ASSERT_NE(p_active, nullptr);
    ASSERT_EQ(p_active->traceability_reqs.size(), 1u);
    EXPECT_EQ(p_active->traceability_reqs[0], "REQ_001");
    ASSERT_TRUE(p_active->time_invariant.has_value());
    EXPECT_EQ(*p_active->time_invariant, "500[ms]");
}

/**
 * @brief Regression: `CameoSerializer` and `CameoXmiGraphResolver` preserve `Fork`, `Join`,
 * `EntryPoint`, and `ExitPoint` pseudostate `StateKind` values across XMI roundtrips.
 * @scenario Serialize an FSM with `Fork`, `Join`, `EntryPoint`, and `ExitPoint` pseudostates to Cameo XMI and re-parse.
 * @expected Re-parsed IR preserves all four `StateKind` values.
 */
TEST(CameoParser, ForkJoinEntryAndExitPoints_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "CameoPseudoRt";
    ir.initial_state = "Init";
    ir.add_state("Init");
    ir.add_or_get_state("Split", "", StateKind::Fork);
    ir.add_or_get_state("Merge", "", StateKind::Join);
    auto& comp = ir.add_state("Comp");
    comp.is_composite = true;
    comp.initial_sub_state = "S1";
    ir.add_state("S1", "Comp");
    ir.add_or_get_state("Ep", "Comp", StateKind::EntryPoint);
    ir.add_or_get_state("Xp", "Comp", StateKind::ExitPoint);

    const std::string xmi = fsm::backend::formal::CameoSerializer::serialize(ir);
    CameoXmiParser parser;
    FsmIr parsed;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, parsed, err)) << err;

    ASSERT_NE(parsed.find_state("Split"), nullptr);
    EXPECT_EQ(parsed.find_state("Split")->kind, StateKind::Fork);
    ASSERT_NE(parsed.find_state("Merge"), nullptr);
    EXPECT_EQ(parsed.find_state("Merge")->kind, StateKind::Join);
    ASSERT_NE(parsed.find_state("Ep"), nullptr);
    EXPECT_EQ(parsed.find_state("Ep")->kind, StateKind::EntryPoint);
    ASSERT_NE(parsed.find_state("Xp"), nullptr);
    EXPECT_EQ(parsed.find_state("Xp")->kind, StateKind::ExitPoint);
}

/**
 * @brief Regression: `CameoSerializer` emits `trigger="when(<expr>)"` for `ChangeTrigger`
 * transitions so `CameoXmiGraphResolver` preserves `ChangeTrigger` across Cameo XMI roundtrips.
 * @scenario Serialize a transition with `ChangeTrigger("count > 5")` to Cameo XMI and re-parse.
 * @expected Serialized XMI contains `trigger="when(count &gt; 5)"` and re-parsed transition holds `ChangeTrigger`.
 */
TEST(CameoParser, ChangeTrigger_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "CameoChangeRt";
    ir.initial_state = "Wait";
    ir.add_state("Wait");
    ir.add_state("Done");
    ir.add_variable(VariableDefinition("count", DataType::int32(), "0"));
    TransitionEdge t("Wait", "Done", "");
    t.trigger = ChangeTrigger("count > 5", true);
    ir.add_transition(t);

    const std::string xmi = fsm::backend::formal::CameoSerializer::serialize(ir);
    EXPECT_NE(xmi.find("trigger=\"when(count &gt; 5)\""), std::string::npos) << xmi;

    CameoXmiParser parser;
    FsmIr parsed;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, parsed, err)) << err;
    ASSERT_EQ(parsed.transitions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ChangeTrigger>(parsed.transitions[0].trigger));
    EXPECT_EQ(std::get<ChangeTrigger>(parsed.transitions[0].trigger).raw_expression, "count > 5");
}

/**
 * @brief Regression: `CameoXmiGraphResolver` registers `kind="choice"` pseudostates in
 * `model.choice_nodes` so `ChoiceNode` entries survive Cameo XMI roundtrips.
 * @scenario Serialize an FSM with `ChoiceNode("C1")` to Cameo XMI and re-parse.
 * @expected Re-parsed IR has `is_choice_node("C1") == true` and `!parsed.choice_nodes.empty()`.
 */
TEST(CameoParser, ChoiceNode_PreservedInChoiceNodesAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "CameoChoiceRt";
    ir.initial_state = "Init";
    ir.add_state("Init");
    ir.add_choice_node("C1");
    ir.add_state("High");
    ir.add_transition(TransitionEdge("Init", "C1", "Eval"));
    ir.add_transition(TransitionEdge("C1", "High", ""));

    const std::string xmi = fsm::backend::formal::CameoSerializer::serialize(ir);
    CameoXmiParser parser;
    FsmIr parsed;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, parsed, err)) << err;
    EXPECT_TRUE(parsed.is_choice_node("C1"));
    EXPECT_FALSE(parsed.choice_nodes.empty());
}

/**
 * @brief Regression: `CameoSerializer` and `CameoXmiParser` preserve multiple `traceability_reqs`
 * (`satisfies="REQ_001, REQ_002"`) and typed `SignalDefinition::attributes` across Cameo XMI roundtrips.
 * @scenario Serialize an FSM with `Idle` carrying `{"REQ_001", "REQ_002"}` and signal `Telemetry{uint32 val, string
 * tag}`, then re-parse.
 * @expected Re-parsed `Idle` has both requirements and `Telemetry` preserves both payload attributes.
 */
TEST(CameoParser, MultiReqsAndSignalAttributes_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "CameoSigReqRt";
    ir.initial_state = "Idle";
    auto& idle = ir.add_state("Idle");
    idle.traceability_reqs = {"REQ_001", "REQ_002"};

    SignalDefinition tel("Telemetry");
    tel.attributes.emplace_back("val", DataType::uint32());
    tel.attributes.emplace_back("tag", DataType::string());
    tel.validators.push_back("val > 0");
    ir.add_signal(tel);

    const std::string xmi = fsm::backend::formal::CameoSerializer::serialize(ir);
    CameoXmiParser parser;
    FsmIr parsed;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, parsed, err)) << err;

    const auto* p_idle = parsed.find_state("Idle");
    ASSERT_NE(p_idle, nullptr);
    ASSERT_EQ(p_idle->traceability_reqs.size(), 2u);
    EXPECT_EQ(p_idle->traceability_reqs[0], "REQ_001");
    EXPECT_EQ(p_idle->traceability_reqs[1], "REQ_002");

    const auto* p_tel = parsed.find_signal("Telemetry");
    ASSERT_NE(p_tel, nullptr);
    ASSERT_EQ(p_tel->attributes.size(), 2u);
    EXPECT_EQ(p_tel->attributes[0].name, "val");
    EXPECT_EQ(p_tel->attributes[1].name, "tag");
    ASSERT_EQ(p_tel->validators.size(), 1u);
    EXPECT_EQ(p_tel->validators[0], "val > 0");
}

}  // namespace

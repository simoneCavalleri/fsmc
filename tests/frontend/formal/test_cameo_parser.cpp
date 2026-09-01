#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify Cameo Systems Modeler OMG XMI 2.x standard XML schema parsing.
 *
 * Scenario:
 * - Parse XML containing `<uml:StateMachine>`, `<subvertex xmi:type="uml:State">`, `<transition>`, `<trigger>`,
 * `<effect>`.
 * - Verify initial pseudostate and transitions are mapped accurately to FsmIr.
 */
TEST(CameoParserTest, BasicXmiParsing) {
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
    EXPECT_EQ(model.events.size(), 3u);
    EXPECT_EQ(model.guards.size(), 1u);
    EXPECT_EQ(model.actions.size(), 3u);
    EXPECT_EQ(model.transitions.size(), 3u);
}

/**
 * @brief Test Intent: Verify Cameo nested composite regions and choice pseudostates (`kind="choice"`).
 *
 * Scenario:
 * - Parse XML with nested regions and choice nodes.
 * - Verify choice resolution and composite state structure.
 */
TEST(CameoParserTest, CompositeAndChoiceParsing) {
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
 * @brief Test Intent: Verify attribute-style XML transition properties (`trigger=...`, `guard=...`, `effect=...`).
 *
 * Scenario:
 * - Parse XML with inline attributes instead of child XML nodes.
 * - Verify actions and guards are identified accurately.
 */
TEST(CameoParserTest, AttributeStyleEffectAndActionParsing) {
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
 * @brief Test Intent: Verify Cameo `<entry>`, `<doActivity>`, `<exit>`, and `<deferrableTrigger>` parsing.
 *
 * Scenario:
 * - Parse XML containing state lifecycle activities.
 * - Verify actions and deferred events are recorded on StateNode.
 */
TEST(CameoParserTest, NativeEntryExitAndDoActivity) {
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
 * @brief Test Intent: Verify XML entity decoding (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&apos;`) and identifier
 * sanitization.
 *
 * Scenario:
 * - Parse XML with encoded entity characters in attribute values.
 * - Verify entities are unescaped before identifier sanitization.
 */
TEST(CameoParserTest, XmlEntityDecodingInNamesAndGuards) {
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
    EXPECT_EQ(model.transitions[0].action, "Action_Special_Tag");
}

/**
 * @brief Test Intent: Verify Cameo UML pseudostates (`shallowHistory`, `deepHistory`, `junction`).
 *
 * Scenario:
 * - Parse XML with UML pseudostates and verify parsing completes cleanly.
 */
TEST(CameoParserTest, HistoryAndJunctionPseudostates) {
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

}  // namespace

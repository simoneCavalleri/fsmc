#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cameo_xmi_parser.hpp"
#include "codegen/cpp_generator.hpp"
#include "codegen/fsm_model.hpp"
#include "codegen/fsm_validator.hpp"

using namespace fsm::codegen;

void test_cameo_basic_xmi_parsing() {
    std::cout << "[TEST] Running test_cameo_basic_xmi_parsing...\n";

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
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(xmi_content, model, err);

    assert(is_parsed);
    assert(model.name == "DeviceProtocolFSM");
    assert(model.initial_state == "Disconnected");
    assert(model.states.size() == 3);
    assert(model.events.size() == 3);
    assert(model.guards.size() == 1);
    assert(model.actions.size() == 3);
    assert(model.transitions.size() == 3);

    std::cout << "[PASS] test_cameo_basic_xmi_parsing passed.\n";
}

void test_cameo_composite_and_choice_parsing() {
    std::cout << "[TEST] Running test_cameo_composite_and_choice_parsing...\n";

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
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(xmi_content, model, err);

    assert(is_parsed);
    assert(model.name == "SpacecraftMissionFSM");
    assert(model.is_choice_node("ClearanceChoice"));

    const auto* inflight = model.find_state("InFlight");
    assert(inflight != nullptr);
    assert(inflight->is_composite);

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    opts.standalone = true;

    const std::string code = CppGenerator::generate_header(model, opts);
    assert(!code.empty());
    assert(code.find("struct Standby") != std::string::npos);
    assert(code.find("struct InFlight") != std::string::npos);
    assert(code.find("struct ArmEnginesAction") != std::string::npos);

    std::cout << "[PASS] test_cameo_composite_and_choice_parsing passed.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "     RUNNING CAMEO XMI PARSER TESTS     \n";
    std::cout << "========================================\n";

    test_cameo_basic_xmi_parsing();
    test_cameo_composite_and_choice_parsing();

    std::cout << "========================================\n";
    std::cout << "     ALL CAMEO TESTS PASSED (2/2)!      \n";
    std::cout << "========================================\n";
    return 0;
}

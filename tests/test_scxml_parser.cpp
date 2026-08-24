#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/scxml_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::codegen;

namespace {

TEST(ScxmlParserTest, BasicScxmlParsingWithInternalTransitions) {
    const std::string scxml_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Disconnected" name="ScxmlConnectionFSM">
  <state id="Disconnected">
    <transition event="ConnectCmd" cond="HasNetworkGuard" target="Connecting">
      <send event="InitSocketAction"/>
    </transition>
    <transition event="ConnectCmd" cond="NoNetworkGuard" target="Disconnected">
      <send event="LogErrorAction"/>
    </transition>
  </state>
  <state id="Connecting">
    <transition event="HandshakeOkEvent" target="Connected">
      <send event="SetupSessionAction"/>
    </transition>
    <transition event="HandshakeFailedEvent" target="Disconnected">
      <send event="CleanupAction"/>
    </transition>
  </state>
  <state id="Connected">
    <transition event="Ping">
      <send event="ResetWatchdogAction"/>
    </transition>
    <transition event="DisconnectCmd" target="Disconnected">
      <send event="CloseSocketAction"/>
    </transition>
  </state>
</scxml>
)";

    ScxmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(scxml_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "ScxmlConnectionFSM");
    EXPECT_EQ(model.initial_state, "Disconnected");
    EXPECT_EQ(model.states.size(), 3u);
    EXPECT_EQ(model.guards.size(), 2u);
    EXPECT_EQ(model.actions.size(), 6u);

    // Verify internal transition for Ping
    bool found_internal = false;
    for (const auto& trans : model.transitions) {
        if (trans.event == "Ping" && trans.kind == TransitionEdgeKind::Internal) {
            found_internal = true;
            break;
        }
    }
    EXPECT_TRUE(found_internal);
}

TEST(ScxmlParserTest, UserReportedIndustrialPressSnippet) {
    const std::string scxml_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Idle" name="GeneratedFSM">
  <state id="Idle">
    <transition event="PowerOnCmd" cond="SafetyOk" target="Initializing" action="LogPowerOn"/>
  </state>
  <state id="Initializing">
    <transition event="InitDone" target="Ready" action="StoreDiagnostics"/>
    <transition event="AbortCmd" target="Idle" action="Cleanup"/>
  </state>
  <state id="Ready">
    <transition event="StartCmd" cond="ToolLoaded" target="Running" action="EngageDrive"/>
  </state>
  <state id="Running">
    <transition event="PauseCmd" target="Paused" action="HoldPosition"/>
    <transition event="EStopEvent" target="Faulted" action="EmergencyBrake"/>
    <transition event="StopCmd" target="Idle" action="DisengageDrive"/>
  </state>
  <state id="Paused">
    <transition event="ResumeCmd" cond="SafetyOk" target="Running" action="ReleaseHold"/>
  </state>
  <state id="Faulted">
    <transition event="ResetFaultCmd" target="Idle" action="ClearFault"/>
  </state>
</scxml>)";

    ScxmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(scxml_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.states.size(), 6U);
    EXPECT_EQ(model.transitions.size(), 9U);
    EXPECT_EQ(model.initial_state, "Idle");
    EXPECT_EQ(model.actions.size(), 9U);
    EXPECT_EQ(model.guards.size(), 2U);
}

TEST(ScxmlParserTest, AttributePermutationsAndSelfClosingStates) {
    const std::string scxml_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="S1">
  <state id="S1">
    <transition target="S2" cond="GuardA" event="Evt1" action="Act1"/>
    <transition action="Act2" event="Evt2" target="S3"/>
  </state>
  <state id="S2">
    <transition target="S3" event="Evt3"/>
  </state>
  <state id="S3"/>
</scxml>)";

    ScxmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(scxml_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.states.size(), 3U);
    EXPECT_EQ(model.transitions.size(), 3U);
    EXPECT_EQ(model.actions.size(), 2U);
}

}  // namespace

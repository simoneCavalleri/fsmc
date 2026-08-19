#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cpp_generator.hpp"
#include "codegen/fsm_model.hpp"
#include "codegen/scxml_parser.hpp"

using namespace fsm::codegen;

void test_scxml_basic() {
    std::cout << "[TEST] Running test_scxml_basic...\n";

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
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(scxml_content, model, err);

    assert(is_parsed);
    assert(model.name == "ScxmlConnectionFSM");
    assert(model.initial_state == "Disconnected");
    assert(model.states.size() == 3);
    assert(model.guards.size() == 2);
    assert(model.actions.size() == 6);

    // Verify internal transition for Ping
    bool found_internal = false;
    for (const auto& trans : model.transitions) {
        if (trans.event == "Ping" && trans.kind == TransitionKind::Internal) {
            found_internal = true;
            break;
        }
    }
    assert(found_internal);

    std::cout << "[PASS] test_scxml_basic passed.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "        RUNNING SCXML PARSER TESTS      \n";
    std::cout << "========================================\n";

    test_scxml_basic();

    std::cout << "========================================\n";
    std::cout << "       ALL SCXML TESTS PASSED!          \n";
    std::cout << "========================================\n";
    return 0;
}

#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cpp_generator.hpp"
#include "codegen/fsm_model.hpp"
#include "codegen/json_parser.hpp"

using namespace fsm::codegen;

void test_json_basic() {
    std::cout << "[TEST] Running test_json_basic...\n";

    const std::string json_content = R"({
  "id": "JsonConnectionFSM",
  "initial": "Disconnected",
  "states": {
    "Disconnected": {
      "on": {
        "ConnectCmd": {
          "target": "Connecting",
          "cond": "HasNetworkGuard",
          "actions": ["InitSocketAction"]
        }
      }
    },
    "Connecting": {
      "on": {
        "HandshakeOkEvent": {
          "target": "Connected",
          "actions": ["SetupSessionAction"]
        },
        "HandshakeFailedEvent": "Disconnected"
      }
    },
    "Connected": {
      "on": {
        "DisconnectCmd": {
          "target": "Disconnected",
          "actions": ["CloseSocketAction"]
        }
      }
    }
  }
})";

    JsonStateParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(json_content, model, err);
    (void)is_parsed;

    assert(is_parsed);
    assert(model.name == "JsonConnectionFSM");
    assert(model.initial_state == "Disconnected");
    assert(model.states.size() == 3);
    assert(model.events.size() == 4);
    assert(model.guards.size() == 1);
    assert(model.actions.size() == 3);

    std::cout << "[PASS] test_json_basic passed.\n";
}

void test_json_composite() {
    std::cout << "[TEST] Running test_json_composite...\n";

    const std::string json_content = R"({
  "id": "MissionFSM",
  "initial": "Standby",
  "states": {
    "Standby": {
      "on": {
        "LaunchCmd": "InFlight"
      }
    },
    "InFlight": {
      "initial": "Ascending",
      "states": {
        "Ascending": {
          "on": {
            "AltitudeReached": "Cruising"
          }
        },
        "Cruising": {}
      }
    }
  }
})";

    JsonStateParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(json_content, model, err);
    (void)is_parsed;

    assert(is_parsed);
    assert(model.states.size() == 4);
    const auto* inflight = model.find_state("InFlight");
    (void)inflight;
    assert(inflight != nullptr);
    assert(inflight->is_composite);
    assert(inflight->initial_sub_state == "Ascending");

    std::cout << "[PASS] test_json_composite passed.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "         RUNNING JSON PARSER TESTS      \n";
    std::cout << "========================================\n";

    test_json_basic();
    test_json_composite();

    std::cout << "========================================\n";
    std::cout << "       ALL JSON TESTS PASSED!           \n";
    std::cout << "========================================\n";
    return 0;
}

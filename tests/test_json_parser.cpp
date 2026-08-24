#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/json_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::codegen;

namespace {

TEST(JsonParserTest, BasicJsonParsing) {
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
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(json_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "JsonConnectionFSM");
    EXPECT_EQ(model.initial_state, "Disconnected");
    EXPECT_EQ(model.states.size(), 3u);
    EXPECT_EQ(model.events.size(), 4u);
    EXPECT_EQ(model.guards.size(), 1u);
    EXPECT_EQ(model.actions.size(), 3u);
}

TEST(JsonParserTest, CompositeStatesParsing) {
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
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(json_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.states.size(), 4u);
    const auto* inflight = model.find_state("InFlight");
    ASSERT_NE(inflight, nullptr);
    EXPECT_TRUE(inflight->is_composite);
    EXPECT_EQ(inflight->initial_sub_state, "Ascending");
}

TEST(JsonParserTest, ArrayTransitionsPerEvent) {
    const std::string json_content = R"({
  "id": "MultiTransFSM",
  "initial": "Disconnected",
  "states": {
    "Disconnected": {
      "on": {
        "ConnectCmd": [
          { "target": "Connecting", "guard": "HasNetworkGuard", "action": "InitSocketAction" },
          { "target": "Disconnected", "guard": "NoNetworkGuard", "action": "LogErrorAction" }
        ]
      }
    },
    "Connecting": {}
  }
})";

    JsonStateParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(json_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.states.size(), 2U);
    EXPECT_EQ(model.transitions.size(), 2U);
    EXPECT_EQ(model.actions.size(), 2U);
    EXPECT_EQ(model.guards.size(), 2U);
}

TEST(JsonParserTest, DocumentInsertionOrderPreservation) {
    const std::string json_content = R"({
  "id": "OrderFSM",
  "initial": "ZetaState",
  "states": {
    "ZetaState": {
      "on": { "Evt1": "AlphaState" }
    },
    "AlphaState": {
      "on": { "Evt2": "MuState" }
    },
    "MuState": {
      "on": { "Evt3": "BetaState" }
    },
    "BetaState": {}
  }
})";

    JsonStateParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(json_content, model, err)) << "Error: " << err;

    ASSERT_EQ(model.states.size(), 4U);
    EXPECT_EQ(model.states[0].name, "ZetaState");
    EXPECT_EQ(model.states[1].name, "AlphaState");
    EXPECT_EQ(model.states[2].name, "MuState");
    EXPECT_EQ(model.states[3].name, "BetaState");
}

}  // namespace

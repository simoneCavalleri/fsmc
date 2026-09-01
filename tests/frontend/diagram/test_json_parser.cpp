#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/diagram/json_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify XState-compatible JSON statechart format parsing.
 *
 * Scenario:
 * - Parse JSON state machine with states, `"on"` event maps, target strings, guards, and action lists.
 * - Verify IR elements are populated accurately.
 */
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

/**
 * @brief Test Intent: Verify nested composite states within JSON schema.
 *
 * Scenario:
 * - Parse JSON with nested `"states"` property inside `"InFlight"`.
 * - Verify composite state flags and sub-state parent mappings.
 */
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

/**
 * @brief Test Intent: Verify array of conditional transitions per event key in JSON.
 *
 * Scenario:
 * - Parse `"ConnectCmd": [ { target: ..., guard: ... }, { target: ..., guard: ... } ]`.
 * - Verify multiple transitions for the same event trigger are captured.
 */
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

/**
 * @brief Test Intent: Verify document insertion order preservation of state definitions in JSON.
 *
 * Scenario:
 * - Define states in specific order: ZetaState -> AlphaState -> MuState -> BetaState.
 * - Verify FsmIr preserves this exact ordering.
 */
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

/**
 * @brief Test Intent: Verify parsing of v0.4.0 typed ports and range constraints in JSON schema.
 */
TEST(JsonParserTest, ParsePortsAndContracts) {
    const std::string json_content = R"({
  "id": "DualPortFSM",
  "initial": "Idle",
  "ports": [
    {
      "name": "sensor_in",
      "type": "float",
      "direction": "in",
      "min": 0.0,
      "max": 100.0,
      "constraint": "self >= 0.0 and self <= 100.0"
    },
    {
      "name": "actuator_out",
      "type": "float",
      "direction": "out",
      "min": 0.0,
      "max": 200.0,
      "constraint": "self >= 0.0 and self <= 200.0"
    }
  ],
  "states": {
    "Idle": {
      "on": {
        "Trigger": "Running"
      }
    },
    "Running": {}
  }
})";

    JsonStateParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(json_content, model, err)) << err;

    ASSERT_EQ(model.ports.size(), 2u);
    const auto* in_p = model.find_port("sensor_in");
    ASSERT_NE(in_p, nullptr);
    EXPECT_TRUE(in_p->is_in());
    EXPECT_FALSE(in_p->is_out());
    EXPECT_DOUBLE_EQ(in_p->min_value.value_or(0.0), 0.0);
    EXPECT_DOUBLE_EQ(in_p->max_value.value_or(0.0), 100.0);
    EXPECT_EQ(in_p->constraint, "self >= 0.0 and self <= 100.0");

    const auto* out_p = model.find_port("actuator_out");
    ASSERT_NE(out_p, nullptr);
    EXPECT_TRUE(out_p->is_out());
    EXPECT_FALSE(out_p->is_in());
    EXPECT_DOUBLE_EQ(out_p->min_value.value_or(0.0), 0.0);
    EXPECT_DOUBLE_EQ(out_p->max_value.value_or(0.0), 200.0);
}

}  // namespace

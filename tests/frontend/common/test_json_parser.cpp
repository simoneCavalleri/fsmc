/**
 * @file test_json_parser.cpp
 * @brief Unit tests for front-end JSON statechart parser, composite states, transitions arrays, and port constraints.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/common/json_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::frontend;
using namespace fsm::backend::cpp;
using namespace fsm::backend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify XState-compatible JSON statechart format parsing.
 * @scenario JSON state machine with states, 'on' event maps, target strings, guards, and action lists.
 * @expected FsmIr is populated with matching states, transitions, signals, guards, and actions.
 */
TEST(JsonParser, StandardStatechartSchema_ExtractsStatesTransitionsGuardsAndActions) {
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

    JsonParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(json_content, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.name, "JsonConnectionFSM");
    EXPECT_EQ(model.initial_state, "Disconnected");
    EXPECT_EQ(model.states.size(), 3u);
    EXPECT_EQ(model.signals.size(), 4u);
    EXPECT_EQ(model.guards.size(), 1u);
    EXPECT_EQ(model.actions.size(), 3u);
}

/**
 * @brief Verify nested composite states within JSON schema.
 * @scenario JSON with nested 'states' property inside state 'InFlight'.
 * @expected State is marked composite, sub-states are linked, and initial sub-state is set to 'Ascending'.
 */
TEST(JsonParser, NestedStatesProperty_SynthesizesCompositeHierarchy) {
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

    JsonParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(json_content, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.states.size(), 4u);
    const auto* inflight = model.find_state("InFlight");
    ASSERT_NE(inflight, nullptr);
    EXPECT_TRUE(inflight->is_composite);
    EXPECT_EQ(inflight->initial_sub_state, "Ascending");
}

/**
 * @brief Verify array of conditional transitions per event key in JSON.
 * @scenario Parse 'ConnectCmd' mapped to an array of conditional transition objects.
 * @expected Multiple transitions with respective guards and actions are created for the same trigger.
 */
TEST(JsonParser, ArrayOfTransitionsPerEventKey_CapturesMultipleConditionalBranches) {
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

    JsonParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(json_content, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.states.size(), 2U);
    EXPECT_EQ(model.transitions.size(), 2U);
    EXPECT_EQ(model.actions.size(), 2U);
    EXPECT_EQ(model.guards.size(), 2U);
}

/**
 * @brief Verify document insertion order preservation of state definitions in JSON.
 * @scenario Define states in specific order: ZetaState -> AlphaState -> MuState -> BetaState.
 * @expected FsmIr preserves exact document encounter order in its states vector.
 */
TEST(JsonParser, StateDefinitionsSequence_PreservesDocumentInsertionOrder) {
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

    JsonParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(json_content, model, err)) << "Error: " << err.render_to_string();

    ASSERT_EQ(model.states.size(), 4U);
    EXPECT_EQ(model.states[0].name, "ZetaState");
    EXPECT_EQ(model.states[1].name, "AlphaState");
    EXPECT_EQ(model.states[2].name, "MuState");
    EXPECT_EQ(model.states[3].name, "BetaState");
}

/**
 * @brief Verify parsing of typed ports and range constraints in JSON schema.
 * @scenario JSON schema defining 'sensor_in' (InPort, range [0, 100]) and 'actuator_out' (OutPort, range [0, 200]).
 * @expected PortDefinition instances populated with direction, numeric bounds, and constraint text.
 */
TEST(JsonParser, DirectionalPortsWithIntervalBounds_PopulatesPortDefinitions) {
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

    JsonParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(json_content, model, err)) << err.render_to_string();

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

/**
 * @brief Verify JsonParser / FsmIrDeserializer converts XState "type": "parallel" into StateKind::Parallel and
 * orthogonal_regions.
 * @scenario XState JSON state Active with "type": "parallel" and two child region states Reg1, Reg2.
 * @expected Active has StateKind::Parallel and 2 orthogonal_regions with Reg1 and Reg2.
 */
TEST(JsonParserTest, DeserializesXStateParallelTypeIntoOrthogonalRegions) {
    const std::string json_content = R"json(
{
  "id": "ParJson",
  "initial": "Active",
  "states": {
    "Active": {
      "type": "parallel",
      "states": {
        "Reg1": { "initial": "A1", "states": { "A1": { "on": { "NEXT": "A2" } }, "A2": {} } },
        "Reg2": { "initial": "B1", "states": { "B1": { "on": { "SYNC": "B2" } }, "B2": {} } }
      }
    }
  }
}
)json";

    JsonParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(json_content, model, err)) << err.render_to_string();

    const StateNode* active = model.find_state("Active");
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->kind, StateKind::Parallel);
    ASSERT_EQ(active->orthogonal_regions.size(), 2u);
}

/**
 * @brief Regression: XState JSON transition action strings with assignments (`"x = 42"`) populate `assignments`.
 * @scenario Parse an XState JSON machine with `"actions": ["x = 42"]` on a transition.
 * @expected `transition_action->assignments` contains `x = 42`.
 */
TEST(JsonParser, XStateActionAssignments_PopulatedInActionSignature) {
    const std::string json_content = R"json({
  "id": "JsonAssign",
  "initial": "Idle",
  "context": { "x": 0 },
  "states": {
    "Idle": {
      "on": {
        "EvGo": { "target": "Done", "cond": "x == 0", "actions": ["x = 42"] }
      }
    },
    "Done": {}
  }
})json";

    JsonParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(json_content, model, err)) << err.render_to_string();
    ASSERT_EQ(model.transitions.size(), 1u);
    ASSERT_TRUE(model.transitions[0].transition_action.has_value());
    ASSERT_EQ(model.transitions[0].transition_action->assignments.size(), 1u);
    EXPECT_EQ(model.transitions[0].transition_action->assignments[0].target.name, "x");
    EXPECT_EQ(model.transitions[0].transition_action->assignments[0].expression, "42");
}

/**
 * @brief Regression: XState JSON `entry` and `exit` string assignments populate `ActionSignature::assignments`.
 * @scenario Parse an XState JSON statechart with `"entry": ["x = 1"]` and `"exit": ["x += 10"]`.
 * @expected `entry_actions` and `exit_actions` contain structured `ActionAssignment` entries.
 */
TEST(JsonParser, XStateLifecycleAssignments_PopulatedInEntryAndExitActions) {
    const std::string json_content = R"json({
  "id": "JsonLife",
  "initial": "Idle",
  "context": { "x": 0 },
  "states": {
    "Idle": {
      "entry": ["x = 1"],
      "exit": ["x += 10"],
      "on": { "EvGo": "Done" }
    },
    "Done": {
      "entry": ["x += 100"]
    }
  }
})json";

    JsonParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(json_content, model, err)) << err.render_to_string();

    const StateNode* idle = model.find_state("Idle");
    ASSERT_NE(idle, nullptr);
    ASSERT_EQ(idle->entry_actions.size(), 1u);
    ASSERT_EQ(idle->entry_actions[0].assignments.size(), 1u);
    EXPECT_EQ(idle->entry_actions[0].assignments[0].target.name, "x");
    EXPECT_EQ(idle->entry_actions[0].assignments[0].expression, "1");

    ASSERT_EQ(idle->exit_actions.size(), 1u);
    ASSERT_EQ(idle->exit_actions[0].assignments.size(), 1u);
    EXPECT_EQ(idle->exit_actions[0].assignments[0].target.name, "x");
    EXPECT_EQ(idle->exit_actions[0].assignments[0].expression, "x + 10");
}

}  // namespace

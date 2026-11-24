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

/**
 * @brief Regression (F2.2): Verify SimpleJsonParser rejects trailing garbage, enforces max nesting depth,
 * and handles deep valid nesting in O(N) time without exponential blow-up.
 */
TEST(JsonParser, Hardening_RejectsTrailingGarbageAndExcessiveDepthLinearly) {
    JsonParser parser;

    // 1. Trailing garbage after valid JSON root must be rejected
    {
        FsmIr model;
        fsm::diagnostic::DiagnosticEngine diag;
        EXPECT_FALSE(parser.parse(R"({"id": "M", "states": {"A": {}}} trailing)", model, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 2. Deep nesting exceeding kMaxJsonDepth (128) must be cleanly rejected
    {
        std::string deep_json;
        for (int i = 0; i < 160; ++i) {
            deep_json += "{\"a\":";
        }
        deep_json += "1";
        for (int i = 0; i < 160; ++i) {
            deep_json += "}";
        }
        FsmIr model;
        fsm::diagnostic::DiagnosticEngine diag;
        EXPECT_FALSE(parser.parse(deep_json, model, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 3. 35-level nested JSON (would OOM under O(2^35) duplication) must parse in O(N) time
    {
        std::string nested = R"({"id":"Deep","initial":"S0","states":{"S0":{},"S1":{}},"attributes":{"x":"1"})";
        nested += ",\"extra\":";
        for (int i = 0; i < 35; ++i) {
            nested += "{\"k\":";
        }
        nested += "42";
        for (int i = 0; i < 35; ++i) {
            nested += "}";
        }
        nested += "}";
        FsmIr model;
        fsm::diagnostic::DiagnosticEngine diag;
        EXPECT_TRUE(parser.parse(nested, model, diag)) << diag.render_to_string();
        EXPECT_EQ(model.name, "Deep");
    }
}

/**
 * @brief Verify full XState v5 conformance: guarded/array `after`, `always` completion transitions, `history`, and
 * `reenter`.
 * @scenario Parse an XState v5 machine using `after` with guard/actions and array branches, `always`, `type:
 * "history"`, and `reenter: true`.
 * @expected All triggers, guards, action assignments, history flags, and transition kinds map accurately into FsmIr.
 */
TEST(JsonParser, XStateV5_AfterGuardsActionsAlwaysHistoryAndReenter) {
    const std::string json_content = R"json({
  "id": "XStateV5Demo",
  "initial": "Armed",
  "context": { "retries": 0 },
  "states": {
    "Armed": {
      "after": {
        "250": [
          { "target": "Timeout", "guard": "retries >= 3", "actions": ["retries = 0"] },
          { "target": "Armed", "reenter": true, "actions": ["retries += 1"] }
        ]
      }
    },
    "Timeout": {
      "always": [
        { "target": "Done", "guard": "retries == 0" }
      ]
    },
    "Done": {
      "type": "final"
    },
    "Saved": {
      "type": "history",
      "history": "deep"
    }
  }
})json";

    JsonParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine diag;
    ASSERT_TRUE(parser.parse(json_content, model, diag)) << diag.render_to_string();

    ASSERT_EQ(model.transitions.size(), 3u);

    // Branch 1 of after[250]
    const auto& t0 = model.transitions[0];
    EXPECT_EQ(t0.source, "Armed");
    EXPECT_EQ(t0.target, "Timeout");
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(t0.trigger));
    EXPECT_EQ(std::get<TimeTrigger>(t0.trigger).duration_ms, 250u);
    ASSERT_TRUE(t0.guard.has_value());
    EXPECT_FALSE(t0.guard->empty());
    ASSERT_TRUE(t0.transition_action.has_value());
    ASSERT_EQ(t0.transition_action->assignments.size(), 1u);
    EXPECT_EQ(t0.transition_action->assignments[0].target.name, "retries");
    EXPECT_EQ(t0.transition_action->assignments[0].expression, "0");

    // Branch 2 of after[250] with reenter: true
    const auto& t1 = model.transitions[1];
    EXPECT_EQ(t1.source, "Armed");
    EXPECT_EQ(t1.target, "Armed");
    EXPECT_EQ(t1.kind, TransitionEdgeKind::External);
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(t1.trigger));
    EXPECT_EQ(std::get<TimeTrigger>(t1.trigger).duration_ms, 250u);

    // always transition -> AnonymousTrigger
    const auto& t2 = model.transitions[2];
    EXPECT_EQ(t2.source, "Timeout");
    EXPECT_EQ(t2.target, "Done");
    EXPECT_TRUE(t2.event.empty());
    EXPECT_TRUE(std::holds_alternative<AnonymousTrigger>(t2.trigger));
    ASSERT_TRUE(t2.guard.has_value());

    const auto* done_state = model.find_state("Done");
    ASSERT_NE(done_state, nullptr);
    EXPECT_EQ(done_state->kind, StateKind::Final);

    const auto* saved_state = model.find_state("Saved");
    ASSERT_NE(saved_state, nullptr);
    EXPECT_TRUE(saved_state->has_history);
    EXPECT_TRUE(saved_state->has_deep_history);
}

}  // namespace

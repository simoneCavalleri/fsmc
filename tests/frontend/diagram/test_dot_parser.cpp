/**
 * @file test_dot_parser.cpp
 * @brief Unit test suite for the Graphviz DOT statechart parser.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/diagram/dot_serializer.hpp"
#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::backend::diagram;
using namespace fsm::frontend::diagram;
using namespace fsm::frontend;
using namespace fsm::backend::cpp;
using namespace fsm::backend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify Graphviz DOT graph parsing with transition labels and initial pseudostate.
 * @scenario Parse Graphviz DOT digraph with edge labels formatted as 'event [guard] / action' and point initial node.
 * @expected Initial state assigned to Disconnected and all 3 states and 5 transitions loaded into FsmIr.
 */
TEST(DotParser, BasicDotDigraph_ParsedIntoValidFsmIr) {
    const std::string dot_content = R"(digraph DotConnectionFSM {
    __start__ [shape=point];
    __start__ -> Disconnected;

    Disconnected -> Connecting [label="ConnectCmd [HasNetworkGuard] / InitSocketAction"];
    Disconnected -> Disconnected [label="ConnectCmd [NoNetworkGuard] / LogErrorAction"];
    Connecting -> Connected [label="HandshakeOkEvent / SetupSessionAction"];
    Connecting -> Disconnected [label="HandshakeFailedEvent / CleanupAction"];
    Connected -> Disconnected [label="DisconnectCmd / CloseSocketAction"];
}
)";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;

    EXPECT_EQ(ir.name, "DotConnectionFSM");
    EXPECT_EQ(ir.initial_state_id, "Disconnected");
    EXPECT_EQ(ir.states.size(), 3u);
    EXPECT_EQ(ir.signals.size(), 4u);
    EXPECT_EQ(ir.transitions.size(), 5u);
}

/**
 * @brief Verify DOT subgraph cluster parsing into hierarchical composite states.
 * @scenario Parse DOT graph containing a cluster subgraph 'cluster_InFlight' with sub-start node.
 * @expected InFlight created as Composite StateKind with nested sub-states populated.
 */
TEST(DotParser, ClusterSubgraph_ParsedAsCompositeState) {
    const std::string dot_content = R"(digraph MissionFSM {
    start [shape=point];
    start -> Standby;

    subgraph cluster_InFlight {
        sub_start [shape=point];
        sub_start -> Ascending;
        Ascending -> Cruising [label="AltitudeReached / DeployPanelsAction"];
    }

    Standby -> InFlight [label="AuthorizeCmd / ArmEnginesAction"];
}
)";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;

    EXPECT_EQ(ir.states.size(), 4u);
    const auto* inflight = ir.find_state("InFlight");
    ASSERT_NE(inflight, nullptr);
    EXPECT_EQ(inflight->kind, StateKind::Composite);
}

/**
 * @brief Regression: scoped initial node names (e.g. `initial_Active`) inside cluster subgraphs set
 *        `initial_sub_state` without requiring an explicit `[shape=point]` node declaration.
 * @scenario Parse a DOT digraph containing `initial -> Active` and `initial_Active -> Idle` inside `cluster_Active`.
 * @expected `initial_Active` is not added as a regular state and `Active->initial_sub_state` is `Idle`.
 */
TEST(DotParser, ScopedInitialIdentifiers_PopulateInitialSubstateWithoutPointShape) {
    const std::string dot_content = R"(digraph HierarchyDot {
    initial -> Active;
    subgraph cluster_Active {
        initial_Active -> Idle;
        Idle -> Running [label="Start / begin_work"];
        Running -> Idle [label="Stop / stop_work"];
    }
    Active -> Fault [label="Error"];
}
)";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;

    EXPECT_EQ(ir.initial_state, "Active");
    EXPECT_EQ(ir.find_state("initial_Active"), nullptr);
    const auto* active = ir.find_state("Active");
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->initial_sub_state, "Idle");
    EXPECT_EQ(ir.states.size(), 4u);
}

/**
 * @brief Regression: DOT transition action assignments (`/ x = 42`) populate `ActionSignature::assignments`.
 * @scenario Parse a DOT edge `Idle -> Done [label="EvGo [ok == true] / x = 42"];`.
 * @expected `transition_action->assignments` contains `x = 42`.
 */
TEST(DotParser, TransitionActionAssignments_PopulatedInActionSignature) {
    const std::string dot_content = R"(digraph DotAssign {
    initial -> Idle;
    Idle -> Done [label="EvGo [ok == true] / x = 42"];
}
)";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;
    ASSERT_EQ(ir.transitions.size(), 1u);
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    ASSERT_EQ(ir.transitions[0].transition_action->assignments.size(), 1u);
    EXPECT_EQ(ir.transitions[0].transition_action->assignments[0].target.name, "x");
    EXPECT_EQ(ir.transitions[0].transition_action->assignments[0].expression, "42");
}

/**
 * @brief Regression: DOT state lifecycle assignments (`(entry / x = 10)` / `(exit / x += 1)`) populate
 * `ActionSignature::assignments`.
 * @scenario Parse a DOT node with `(entry / x = 10)` and `(exit / x += 1)` labels.
 * @expected `entry_actions` and `exit_actions` contain populated assignment records.
 */
TEST(DotParser, StateLifecycleAssignments_PopulatedInEntryAndExitActions) {
    const std::string dot_content = R"dot(digraph DotLife {
    __start__ -> Ready;
    Ready [label="Ready\n(entry / x = 10)\n(exit / x += 1)"];
    Done [label="Done"];
    Ready -> Done [label="GO"];
}
)dot";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;

    const auto* ready = ir.find_state("Ready");
    ASSERT_NE(ready, nullptr);
    ASSERT_EQ(ready->entry_actions.size(), 1u);
    ASSERT_EQ(ready->entry_actions[0].assignments.size(), 1u);
    EXPECT_EQ(ready->entry_actions[0].assignments[0].target.name, "x");
    EXPECT_EQ(ready->entry_actions[0].assignments[0].expression, "10");

    ASSERT_EQ(ready->exit_actions.size(), 1u);
    ASSERT_EQ(ready->exit_actions[0].assignments.size(), 1u);
    EXPECT_EQ(ready->exit_actions[0].assignments[0].target.name, "x");
    EXPECT_EQ(ready->exit_actions[0].assignments[0].expression, "x + 1");
}

/**
 * @brief Regression: inline `(prio=N)` inside DOT edge labels is stripped from `TransitionEdge::event`.
 * @scenario Parse a DOT edge with `[label="(prio=5) CellsBalancedEvent / DisableShunts"]`.
 * @expected `trans.priority == 5` and `trans.event == "CellsBalancedEvent"` (no `prio5_` prefix).
 */
TEST(DotParser, InlinePriorityLabel_StripsPrefixFromEventName) {
    const std::string dot_content = R"dot(digraph DotPrio {
    __start__ -> Active;
    Active -> Idle [label="(prio=5) CellsBalancedEvent / DisableShunts"];
}
)dot";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;
    ASSERT_EQ(ir.transitions.size(), 1u);
    EXPECT_EQ(ir.transitions[0].priority, 5u);
    EXPECT_EQ(ir.transitions[0].event, "CellsBalancedEvent");
    EXPECT_EQ(ir.transitions[0].get_action(), "DisableShunts");
}

/**
 * @brief Regression: DOT subgraphs marked with `// @fsm:parallel` or dashed style populate `StateKind::Parallel` and
 * `orthogonal_regions`.
 * @scenario Parse a DOT parallel cluster containing two region subgraphs.
 * @expected Parent state has `StateKind::Parallel` and 2 `orthogonal_regions` with their initial substates.
 */
TEST(DotParser, ParallelCluster_PopulatesOrthogonalRegions) {
    const std::string dot_content = R"dot(digraph ParallelDot {
    __start__ -> Operational;
    subgraph cluster_Operational {
        label="Operational";
        style="rounded,dashed,filled";
        // @fsm:parallel
        subgraph cluster_RegionA {
            label="RegionA";
            __start_RegionA__ [shape=circle, label=""];
            __start_RegionA__ -> A1;
            A1 -> A2 [label="NextA"];
        }
        subgraph cluster_RegionB {
            label="RegionB";
            __start_RegionB__ [shape=circle, label=""];
            __start_RegionB__ -> B1;
            B1 -> B2 [label="NextB"];
        }
    }
}
)dot";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;

    const auto* op = ir.find_state("Operational");
    ASSERT_NE(op, nullptr);
    EXPECT_EQ(op->kind, StateKind::Parallel);
    ASSERT_EQ(op->orthogonal_regions.size(), 2u);
    EXPECT_EQ(op->orthogonal_regions[0].name, "RegionA");
    EXPECT_EQ(op->orthogonal_regions[0].initial_state_id, "A1");
    EXPECT_EQ(op->orthogonal_regions[1].name, "RegionB");
    EXPECT_EQ(op->orthogonal_regions[1].initial_state_id, "B1");
}

/**
 * @brief Regression: DOT `__hist_<State>__` nodes with `label="H"` or `label="H*"` populate `has_history` and
 * `has_deep_history` on the enclosing composite state.
 * @scenario Parse DOT subgraphs containing `__hist_Shallow__ [shape=circle, label="H"]` and
 * `__hist_Deep__ [shape=circle, label="H*"]`.
 * @expected `Shallow` has `has_history=true, has_deep_history=false`; `Deep` has both `true`.
 */
TEST(DotParser, HistoryPseudostate_PopulatesShallowAndDeepHistory) {
    const std::string dot_content = R"dot(digraph DotHistory {
    __start__ -> Shallow;
    subgraph cluster_Shallow {
        label="Shallow";
        __start_Shallow__ [shape=circle, label=""];
        __start_Shallow__ -> S1;
        __hist_Shallow__ [shape=circle, label="H"];
        S1;
    }
    subgraph cluster_Deep {
        label="Deep";
        __start_Deep__ [shape=circle, label=""];
        __start_Deep__ -> D1;
        __hist_Deep__ [shape=circle, label="H*"];
        D1;
    }
    Shallow -> Deep [label="Switch"];
}
)dot";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;

    const auto* shallow = ir.find_state("Shallow");
    const auto* deep = ir.find_state("Deep");
    ASSERT_NE(shallow, nullptr);
    ASSERT_NE(deep, nullptr);
    EXPECT_TRUE(shallow->has_history);
    EXPECT_FALSE(shallow->has_deep_history);
    EXPECT_TRUE(deep->has_history);
    EXPECT_TRUE(deep->has_deep_history);
}

/**
 * @brief Regression: Graphviz DOT quoted node identifiers (`"Idle State" -> "Active State"`) are unquoted
 * and sanitized cleanly instead of failing with "No valid states or transitions extracted".
 * @scenario Parse DOT with quoted node names on initial, attribute, and edge lines.
 * @expected `initial_state == "Idle_State"` and transition `Idle_State -> Active_State` on `Start`.
 */
TEST(DotParser, QuotedNodeNames_ParsedIntoValidStatesAndTransitions) {
    const std::string dot_content = R"dot(digraph DotQuoted {
    __start__ -> "Idle State";
    "Idle State" [label="Idle State"];
    "Idle State" -> "Active State" [label="Start"];
}
)dot";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;
    EXPECT_EQ(ir.initial_state, "Idle_State");
    ASSERT_EQ(ir.transitions.size(), 1u);
    EXPECT_EQ(ir.transitions[0].source, "Idle_State");
    EXPECT_EQ(ir.transitions[0].target, "Active_State");
    EXPECT_EQ(ir.transitions[0].event, "Start");
}

/**
 * @brief Regression: Graphviz DOT `label="..."` with escaped quotes (`\"`) preserves full guard and
 * action expressions instead of truncating at the first escaped quote.
 * @scenario Parse DOT edge `Idle -> Done [label="Go [msg == \"a/b\"] / msg = \"c/d\""];`.
 * @expected Event is `Go`, guard expression is `msg == "a/b"`, and action invocation is `msg = "c/d"`.
 */
TEST(DotParser, EscapedQuotesInEdgeLabel_PreservesGuardAndAction) {
    const std::string dot_content = R"dot(digraph DotEscaped {
    // @fsm:var name=msg type=string init="a/b"
    __start__ -> Idle;
    Idle -> Done [label="Go [msg == \"a/b\"] / msg = \"c/d\""];
}
)dot";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;
    ASSERT_EQ(ir.transitions.size(), 1u);
    EXPECT_EQ(ir.transitions[0].event, "Go");
    ASSERT_EQ(ir.guards.size(), 1u);
    EXPECT_EQ(ir.guards[0].raw_expression, R"(msg == "a/b")");
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    ASSERT_EQ(ir.transitions[0].transition_action->assignments.size(), 1u);
    EXPECT_EQ(ir.transitions[0].transition_action->assignments[0].target, "msg");
    EXPECT_EQ(ir.transitions[0].transition_action->assignments[0].expression, R"("c/d")");
}

/**
 * @brief Regression: Graphviz DOT quoted `subgraph "cluster_Active Mode"` creates composite state
 * `Active_Mode` with nested child states instead of flattening children to top-level.
 * @scenario Parse DOT with `subgraph "cluster_Active Mode"` containing `"Sub A" -> "Sub B"`.
 * @expected `Active_Mode` exists with `is_composite == true` and `Sub_A`/`Sub_B` have `parent_state == "Active_Mode"`.
 */
TEST(DotParser, QuotedSubgraphCluster_CreatesCompositeState) {
    const std::string dot_content = R"dot(digraph DotQuotedCluster {
    __start__ -> Idle;
    subgraph "cluster_Active Mode" {
        "Sub A" -> "Sub B" [label="Next"];
    }
    Idle -> "Sub A" [label="Start"];
}
)dot";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;
    const auto* active = ir.find_state("Active_Mode");
    ASSERT_NE(active, nullptr);
    EXPECT_TRUE(active->is_composite);
    const auto* sub_a = ir.find_state("Sub_A");
    ASSERT_NE(sub_a, nullptr);
    EXPECT_EQ(sub_a->parent_state, "Active_Mode");
    const auto* sub_b = ir.find_state("Sub_B");
    ASSERT_NE(sub_b, nullptr);
    EXPECT_EQ(sub_b->parent_state, "Active_Mode");
}

/**
 * @brief Regression: Graphviz DOT node labels with unparenthesized `entry / ...`, `exit / ...`,
 * `do / ...`, and `defer ...` lines populate state lifecycle actions and deferred events.
 * @scenario Parse DOT state `Idle [label="Idle\nentry / x = 1; y = 2;\nexit / x = 3;\ndo / spin\ndefer Ping"];`.
 * @expected `entry_actions`, `exit_actions`, `do_activity`, and `deferred_events` are populated on `Idle`.
 */
TEST(DotParser, UnparenthesizedLifecycleAndDeferInNodeLabel_PopulatedCleanly) {
    const std::string dot_content = R"dot(digraph DotUnparenLife {
    // @fsm:var name=x type=int init=0
    // @fsm:var name=y type=int init=0
    __start__ -> Idle;
    Idle [label="Idle\nentry / x = 1; y = 2;\nexit / x = 3;\ndo / spin\ndefer Ping"];
    Idle -> Done [label="Go"];
}
)dot";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;
    const auto* idle = ir.find_state("Idle");
    ASSERT_NE(idle, nullptr);
    ASSERT_EQ(idle->entry_actions.size(), 1u);
    EXPECT_EQ(idle->entry_actions[0].assignments.size(), 2u);
    ASSERT_EQ(idle->exit_actions.size(), 1u);
    EXPECT_EQ(idle->exit_actions[0].assignments.size(), 1u);
    EXPECT_EQ(idle->do_activity, "spin");
    ASSERT_EQ(idle->deferred_events.size(), 1u);
    EXPECT_EQ(idle->deferred_events[0], "Ping");
}

/**
 * @brief Regression: Multiple DOT transitions between the same `source` and `target` states
 * assigning to the same variable receive disambiguated synthetic action names (`Idle_Done_act_x`,
 * `Idle_Done_act_x_2`) so their assignments do not collide in C++ codegen.
 * @scenario Parse DOT with `Idle -> Done [label="Ev1 / x = 1"]` and `Idle -> Done [label="Ev2 / x = 2"]`.
 * @expected `transitions[0].transition_action->name != transitions[1].transition_action->name`.
 */
TEST(DotParser, MultipleTransitionsSameEndpoints_DisambiguateAssignmentActionNames) {
    const std::string dot_content = R"dot(digraph DotSameEp {
    // @fsm:var name=x type=int init=0
    __start__ -> Idle;
    Idle -> Done [label="Ev1 / x = 1"];
    Idle -> Done [label="Ev2 / x = 2"];
}
)dot";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;
    ASSERT_EQ(ir.transitions.size(), 2u);
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    ASSERT_TRUE(ir.transitions[1].transition_action.has_value());
    EXPECT_NE(ir.transitions[0].transition_action->name, ir.transitions[1].transition_action->name);
}

/**
 * @brief Regression: `DotParser` reconstructs `TimeTrigger` when parsing `after_<N>ms` /
 * `every_<N>ms` transition labels emitted by `DotSerializer`.
 * @scenario Parse DOT with `Busy -> Idle [label="after_100ms"];`.
 * @expected Transition has a `TimeTrigger` with `duration_in_ms() == 100`.
 */
TEST(DotParser, TimeTriggerEventLabel_PopulatesTimeTrigger) {
    const std::string dot_content = R"dot(digraph DotTimer {
    __start__ -> Busy;
    Busy -> Idle [label="after_100ms"];
}
)dot";

    DotParser parser;
    FsmIr ir;
    std::string err;
    ASSERT_TRUE(parser.parse(dot_content, ir, err)) << "Error: " << err;
    ASSERT_EQ(ir.transitions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(ir.transitions[0].trigger));
    EXPECT_EQ(std::get<TimeTrigger>(ir.transitions[0].trigger).duration_in_ms(), 100u);
}

/**
 * @brief Regression: `DotSerializer` emits `comment="internal"` on `TransitionEdgeKind::Internal`
 * edges so `DotParser` preserves `Internal` kind across Graphviz DOT roundtrips.
 * @scenario Serialize an internal transition `Busy --Ping--> Busy` to DOT and re-parse.
 * @expected Re-parsed transition has `kind == TransitionEdgeKind::Internal`.
 */
TEST(DotParser, InternalTransition_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "DotIntRt";
    ir.initial_state = "Busy";
    ir.add_state("Busy");
    TransitionEdge t("Busy", "Busy", "Ping");
    t.kind = TransitionEdgeKind::Internal;
    ir.add_transition(t);

    const std::string dot = DotSerializer::serialize(ir);
    EXPECT_NE(dot.find("comment=\"internal\""), std::string::npos);

    DotParser parser;
    FsmIr parsed;
    std::string err;
    ASSERT_TRUE(parser.parse(dot, parsed, err)) << err;
    ASSERT_EQ(parsed.transitions.size(), 1u);
    EXPECT_EQ(parsed.transitions[0].kind, TransitionEdgeKind::Internal);
}

/**
 * @brief Regression: `DotSerializer` omits `init=` when `var.initial_value` is empty and preserves
 * port/variable metadata (`min`, `max`, `unit`, `constraint`) across DOT roundtrips.
 * @scenario Serialize an FSM with an uninitialized `string` variable and constrained `in` port, then re-parse.
 * @expected Serialized DOT has no `init=` on the uninitialized variable, and re-parsed IR preserves empty
 * `initial_value`, `min_value`, `max_value`, `physical_unit`, and `constraint`.
 */
TEST(DotParser, UninitializedVarAndPortMetadata_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "DotMetaRt";
    ir.initial_state = "Idle";
    ir.add_state("Idle");

    VariableDefinition msg;
    msg.name = "msg";
    msg.type = DataType::string();
    msg.initial_value.clear();
    msg.physical_unit = "utf8";
    ir.add_variable(msg);

    PortDefinition spd("speed", DataType::int32(), PortDirection::In);
    spd.min_value = 0.0;
    spd.max_value = 250.0;
    spd.constraint = "self >= 0";
    ir.add_port(spd);

    const std::string dot = DotSerializer::serialize(ir);
    EXPECT_EQ(dot.find("init="), std::string::npos) << dot;

    DotParser parser;
    FsmIr parsed;
    std::string err;
    ASSERT_TRUE(parser.parse(dot, parsed, err)) << err;

    const auto* p_msg = parsed.find_variable("msg");
    ASSERT_NE(p_msg, nullptr);
    EXPECT_TRUE(p_msg->initial_value.empty());
    ASSERT_TRUE(p_msg->physical_unit.has_value());
    EXPECT_EQ(*p_msg->physical_unit, "utf8");

    const auto* p_spd = parsed.find_port("speed");
    ASSERT_NE(p_spd, nullptr);
    EXPECT_EQ(p_spd->min_value, std::optional<double>(0.0));
    EXPECT_EQ(p_spd->max_value, std::optional<double>(250.0));
    EXPECT_EQ(p_spd->constraint, "self >= 0");
}

/**
 * @brief Regression: `DotSerializer` emits `// @fsm:state` directives for `traceability_reqs`
 * and `time_invariant` on both leaf and composite states so `DotParser` preserves them across roundtrips.
 * @scenario Serialize an FSM with leaf `Idle` (`REQ_001`) and composite `Active` (`500[ms]`), then re-parse.
 * @expected Re-parsed `Idle` has `traceability_reqs == {"REQ_001"}` and `Active` has `time_invariant == "500[ms]"`.
 */
TEST(DotParser, StateReqsAndTimeInvariant_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "DotStateMetaRt";
    ir.initial_state = "Idle";
    auto& idle = ir.add_state("Idle");
    idle.traceability_reqs.push_back("REQ_001");

    auto& active = ir.add_state("Active");
    active.is_composite = true;
    active.initial_sub_state = "Sub1";
    active.time_invariant = "500[ms]";
    ir.add_state("Sub1", "Active");

    const std::string dot = DotSerializer::serialize(ir);
    DotParser parser;
    FsmIr parsed;
    std::string err;
    ASSERT_TRUE(parser.parse(dot, parsed, err)) << err;

    const auto* p_idle = parsed.find_state("Idle");
    ASSERT_NE(p_idle, nullptr);
    ASSERT_EQ(p_idle->traceability_reqs.size(), 1u);
    EXPECT_EQ(p_idle->traceability_reqs[0], "REQ_001");

    const auto* p_active = parsed.find_state("Active");
    ASSERT_NE(p_active, nullptr);
    ASSERT_TRUE(p_active->time_invariant.has_value());
    EXPECT_EQ(*p_active->time_invariant, "500[ms]");
}

/**
 * @brief Regression: `DotSerializer` emits `kind=...` on `@fsm:state` directives for `Fork`,
 * `Join`, `EntryPoint`, `ExitPoint`, and `Final` states so `DotParser` preserves them across roundtrips.
 * @scenario Serialize an FSM with `Fork`, `Join`, `EntryPoint`, `ExitPoint`, and `Final` states, then re-parse.
 * @expected Re-parsed IR preserves all five `StateKind` values.
 */
TEST(DotParser, PseudostatesAndFinalKind_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "DotPseudoRt";
    ir.initial_state = "Init";
    ir.add_state("Init");
    ir.add_or_get_state("Split", "", StateKind::Fork);
    ir.add_or_get_state("Merge", "", StateKind::Join);
    ir.add_or_get_state("Done", "", StateKind::Final);
    auto& comp = ir.add_state("Comp");
    comp.is_composite = true;
    comp.initial_sub_state = "S1";
    ir.add_state("S1", "Comp");
    ir.add_or_get_state("Ep", "Comp", StateKind::EntryPoint);
    ir.add_or_get_state("Xp", "Comp", StateKind::ExitPoint);

    const std::string dot = DotSerializer::serialize(ir);
    DotParser parser;
    FsmIr parsed;
    std::string err;
    ASSERT_TRUE(parser.parse(dot, parsed, err)) << err;

    ASSERT_NE(parsed.find_state("Split"), nullptr);
    EXPECT_EQ(parsed.find_state("Split")->kind, StateKind::Fork);
    ASSERT_NE(parsed.find_state("Merge"), nullptr);
    EXPECT_EQ(parsed.find_state("Merge")->kind, StateKind::Join);
    ASSERT_NE(parsed.find_state("Done"), nullptr);
    EXPECT_EQ(parsed.find_state("Done")->kind, StateKind::Final);
    ASSERT_NE(parsed.find_state("Ep"), nullptr);
    EXPECT_EQ(parsed.find_state("Ep")->kind, StateKind::EntryPoint);
    ASSERT_NE(parsed.find_state("Xp"), nullptr);
    EXPECT_EQ(parsed.find_state("Xp")->kind, StateKind::ExitPoint);
}

}  // namespace

/**
 * @file test_dot_parser.cpp
 * @brief Unit test suite for the Graphviz DOT statechart parser.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

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

}  // namespace

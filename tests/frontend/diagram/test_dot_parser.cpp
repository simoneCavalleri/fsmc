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
 * @brief Test Intent: Verify Graphviz DOT graph parsing with transition labels and initial pseudostate (`__start__`).
 *
 * Scenario:
 * - Parse Graphviz DOT `digraph` with edge labels formatted as `event [guard] / action`.
 * - Verify initial point node points to Disconnected, and transitions are populated into FsmIr.
 */
TEST(DotParserTest, BasicDotParsing) {
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
 * @brief Test Intent: Verify DOT `subgraph cluster_<Name>` parsing into hierarchical composite states.
 *
 * Scenario:
 * - Parse DOT graph containing a cluster subgraph `cluster_InFlight`.
 * - Verify InFlight is parsed as a Composite StateKind with nested sub-states.
 */
TEST(DotParserTest, CompositeClusterParsing) {
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

}  // namespace

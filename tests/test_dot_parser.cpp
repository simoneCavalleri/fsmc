#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cpp_generator.hpp"
#include "codegen/dot_parser.hpp"
#include "codegen/fsm_model.hpp"

using namespace fsm::codegen;

void test_dot_basic() {
    std::cout << "[TEST] Running test_dot_basic...\n";

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
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(dot_content, model, err);
    (void)is_parsed;

    assert(is_parsed);
    assert(model.name == "DotConnectionFSM");
    assert(model.initial_state == "Disconnected");
    assert(model.states.size() == 3);
    assert(model.events.size() == 4);
    assert(model.guards.size() == 2);
    assert(model.actions.size() == 5);

    std::cout << "[PASS] test_dot_basic passed.\n";
}

void test_dot_composite() {
    std::cout << "[TEST] Running test_dot_composite...\n";

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
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(dot_content, model, err);
    (void)is_parsed;

    assert(is_parsed);
    assert(model.states.size() == 4);
    const auto* inflight = model.find_state("InFlight");
    (void)inflight;
    assert(inflight != nullptr);
    assert(inflight->is_composite);
    assert(inflight->initial_sub_state == "Ascending");

    std::cout << "[PASS] test_dot_composite passed.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "         RUNNING DOT PARSER TESTS       \n";
    std::cout << "========================================\n";

    test_dot_basic();
    test_dot_composite();

    std::cout << "========================================\n";
    std::cout << "       ALL DOT TESTS PASSED!            \n";
    std::cout << "========================================\n";
    return 0;
}

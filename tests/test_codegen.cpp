#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cpp_generator.hpp"
#include "codegen/mermaid_parser.hpp"

using namespace fsm::codegen;

namespace {

void test_cpp17_codegen_generation() {
    std::cout << "[TEST] Running test_cpp17_codegen_generation...\n";

    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        Idle --> Connecting : ConnectCmd [NetworkReady] / InitSocket
        Connecting --> Connected : HandshakeOk / EnableData
        Connected --> Idle : DisconnectCmd / CloseSocket
    )";

    MermaidParser parser;
    FsmModel model;
    model.name = "NetworkFSM";
    model.ns = "net";
    model.context_type = "no_context";

    std::string err;
    const bool is_parsed = parser.parse(mmd, model, err);
    assert(is_parsed);

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp17;
    const std::string code = CppGenerator::generate_header(model, opts);

    assert(code.find("namespace net {") != std::string::npos);
    assert(code.find("template <typename Event, typename State, typename Context>") != std::string::npos);
    assert(code.find("using NetworkFSMTable = fsm::transition_table<") != std::string::npos);

    std::cout << "[PASS] test_cpp17_codegen_generation passed.\n";
}

void test_cpp20_codegen_generation() {
    std::cout << "[TEST] Running test_cpp20_codegen_generation...\n";

    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        Idle --> Connecting : ConnectCmd [NetworkReady] / InitSocket
        Connecting --> Connected : HandshakeOk / EnableData
        Connected --> Idle : DisconnectCmd / CloseSocket
    )";

    MermaidParser parser;
    FsmModel model;
    model.name = "NetworkFSM";
    model.ns = "net";
    model.context_type = "no_context";

    std::string err;
    const bool is_parsed = parser.parse(mmd, model, err);
    assert(is_parsed);

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    const std::string code = CppGenerator::generate_header(model, opts);

    assert(code.find("namespace net {") != std::string::npos);
    assert(code.find("[[nodiscard]] constexpr bool operator()(const auto&") != std::string::npos);
    assert(code.find("constexpr void operator()(const auto&") != std::string::npos);
    assert(code.find("using NetworkFSMTable = fsm::transition_table<") != std::string::npos);

    std::cout << "[PASS] test_cpp20_codegen_generation passed.\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "     RUNNING FSM-GEN CODEGEN TESTS      \n"
              << "========================================\n";

    test_cpp17_codegen_generation();
    test_cpp20_codegen_generation();

    std::cout << "========================================\n"
              << "     ALL CODEGEN TESTS PASSED (2/2)!    \n"
              << "========================================\n";
    return 0;
}

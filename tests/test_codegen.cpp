#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/runtime_exporter.hpp"
#include "fsm/frontend/mermaid_parser.hpp"

using namespace fsm::codegen;
namespace fs = std::filesystem;

namespace {

TEST(CodegenTest, Cpp17CodegenGeneration) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        Idle --> Connecting : ConnectCmd [NetworkReady] / InitSocket
        Connecting --> Connected : HandshakeOk / EnableData
        Connected --> Idle : DisconnectCmd / CloseSocket
    )";

    MermaidParser parser;
    FsmIr model;
    model.name = "NetworkFSM";
    model.ns = "net";
    model.context_type = "no_context";

    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp17;
    const std::string code = CppGenerator::generate_header(model, opts);

    EXPECT_NE(code.find("namespace net {"), std::string::npos);
    EXPECT_NE(code.find("template <typename Event, typename State, typename Context>"), std::string::npos);
    EXPECT_NE(code.find("using NetworkFSMTable = fsm::transition_table<"), std::string::npos);
}

TEST(CodegenTest, Cpp20CodegenGeneration) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        Idle --> Connecting : ConnectCmd [NetworkReady] / InitSocket
        Connecting --> Connected : HandshakeOk / EnableData
        Connected --> Idle : DisconnectCmd / CloseSocket
    )";

    MermaidParser parser;
    FsmIr model;
    model.name = "NetworkFSM";
    model.ns = "net";
    model.context_type = "no_context";

    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    const std::string code = CppGenerator::generate_header(model, opts);

    EXPECT_NE(code.find("namespace net {"), std::string::npos);
    EXPECT_NE(code.find("[[nodiscard]] constexpr bool operator()(const auto&"), std::string::npos);
    EXPECT_NE(code.find("constexpr void operator()(const auto&"), std::string::npos);
    EXPECT_NE(code.find("using NetworkFSMTable = fsm::transition_table<"), std::string::npos);
}

TEST(CodegenTest, RuntimeExporterCpp17AndCpp20) {
    const std::string export_dir_cpp20 = "temp_runtime_export_cpp20";
    const std::string export_dir_cpp17 = "temp_runtime_export_cpp17";
    std::string err;

    // Test C++20 Runtime Export
    EXPECT_TRUE(RuntimeExporter::export_runtime(export_dir_cpp20, CppStandard::Cpp20, err));
    EXPECT_TRUE(fs::exists(export_dir_cpp20 + "/fsm.hpp"));

    // Test C++17 Runtime Export
    EXPECT_TRUE(RuntimeExporter::export_runtime(export_dir_cpp17, CppStandard::Cpp17, err));
    EXPECT_TRUE(fs::exists(export_dir_cpp17 + "/fsm.hpp"));

    // Cleanup
    fs::remove_all(export_dir_cpp20);
    fs::remove_all(export_dir_cpp17);
}

}  // namespace

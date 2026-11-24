/**
 * @file test_emitter_factory.cpp
 * @brief Unit test suite for the multi-format emitter factory.
 */

#include <gtest/gtest.h>

#include "fsm/backend/codegen_backend.hpp"
#include "fsm/backend/emitter_factory.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_manager.hpp"

namespace {

using namespace fsm::backend;
using namespace fsm::ir;

/**
 * @brief Verify query of supported emitter target formats.
 * @scenario Query EmitterFactory for list of all registered emitter formats.
 * @expected At least 9 canonical formats are returned (IR, PlantUML, Mermaid, SysML v2, Cameo, SCXML, JSON, DOT, SMV).
 */
TEST(EmitterFactory, SupportedFormatsList_ContainsAllRegisteredBackends) {
    auto formats = EmitterFactory::supported_formats();
    EXPECT_GE(formats.size(), 9u);
}

/**
 * @brief Verify emission across all supported formats via factory dispatcher.
 * @scenario Construct canonical FsmIr and emit through factory to all 9 supported formats and invalid format.
 * @expected Valid emission strings returned containing format markers for all 9 formats, and empty string for invalid
 * format.
 */
TEST(EmitterFactory, CanonicalModel_EmittedAcrossAllRegisteredFormats) {
    FsmIr ir;
    ir.name = "MissionControlFSM";
    ir.initial_state = "Idle";
    ir.add_state("Idle");
    ir.add_state("Active");
    TransitionEdge edge("Idle", "Active", "StartCmd", "PowerOkGuard", "InitEnginesAction");
    ir.add_transition(std::move(edge));

    // 1. Canonical IR
    std::string ir_out = EmitterFactory::emit_diagram(ir, "ir");
    EXPECT_FALSE(ir_out.empty());
    EXPECT_NE(ir_out.find("\"name\": \"MissionControlFSM\""), std::string::npos);

    // 2. PlantUML
    std::string puml_out = EmitterFactory::emit_diagram(ir, "plantuml");
    EXPECT_FALSE(puml_out.empty());
    EXPECT_NE(puml_out.find("@startuml"), std::string::npos);
    EXPECT_NE(puml_out.find("@enduml"), std::string::npos);

    // 3. Mermaid
    std::string mmd_out = EmitterFactory::emit_diagram(ir, "mermaid");
    EXPECT_FALSE(mmd_out.empty());
    EXPECT_NE(mmd_out.find("stateDiagram-v2"), std::string::npos);

    // 4. SysML v2
    std::string sysml_out = EmitterFactory::emit_diagram(ir, "sysml2");
    EXPECT_FALSE(sysml_out.empty());
    EXPECT_NE(sysml_out.find("state def MissionControlFSM"), std::string::npos);

    // 5. Cameo XMI
    std::string cameo_out = EmitterFactory::emit_diagram(ir, "cameo");
    EXPECT_FALSE(cameo_out.empty());
    EXPECT_NE(cameo_out.find("<xmi:XMI"), std::string::npos);

    // 6. SCXML
    std::string scxml_out = EmitterFactory::emit_diagram(ir, "scxml");
    EXPECT_FALSE(scxml_out.empty());
    EXPECT_NE(scxml_out.find("<scxml"), std::string::npos);

    // 7. JSON
    std::string json_out = EmitterFactory::emit_diagram(ir, "json");
    EXPECT_FALSE(json_out.empty());
    EXPECT_NE(json_out.find("\"initial\": \"Idle\""), std::string::npos);

    // 8. DOT / Graphviz
    std::string dot_out = EmitterFactory::emit_diagram(ir, "dot");
    EXPECT_FALSE(dot_out.empty());
    EXPECT_NE(dot_out.find("digraph MissionControlFSM"), std::string::npos);

    // 9. nuXmv SMV
    std::string smv_out = EmitterFactory::emit_diagram(ir, "smv");
    EXPECT_FALSE(smv_out.empty());
    EXPECT_NE(smv_out.find("MODULE main"), std::string::npos);

    // 10. Unsupported format returns empty string
    std::string invalid_out = EmitterFactory::emit_diagram(ir, "unsupported_nonexistent_format");
    EXPECT_TRUE(invalid_out.empty());
}

/**
 * @brief Verify EmitterFactory format discovery and C++ target code emission.
 * @scenario Query supported_formats(), is_supported_format(), and emit C++ headers via EmitterFactory::emit().
 * @expected Format queries report C++ and diagram formats accurately, and emit(model, "cpp") generates valid C++
 * header.
 */
TEST(EmitterFactory, FormatDiscoveryAndCppTargetEmission_SupportedAndSynthesized) {
    const auto formats = EmitterFactory::supported_formats();
    EXPECT_GE(formats.size(), 11u);

    EXPECT_TRUE(EmitterFactory::is_supported_format("cpp"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("c++"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("c++17"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("c++20"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("plantuml"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("mermaid"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("sysml2"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("scxml"));
    EXPECT_FALSE(EmitterFactory::is_supported_format("nonexistent_target"));

    FsmIr model;
    model.name = "FormatTestFSM";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Active");
    model.add_transition(TransitionEdge("Idle", "Active", "EvStart"));

    const std::string cpp_out = EmitterFactory::emit(model, "cpp");
    EXPECT_FALSE(cpp_out.empty());
    EXPECT_NE(cpp_out.find("FormatTestFSM"), std::string::npos);

    const std::string cpp17_out = EmitterFactory::emit(model, "c++17");
    EXPECT_FALSE(cpp17_out.empty());
    EXPECT_NE(cpp17_out.find("FormatTestFSM"), std::string::npos);
}

namespace {
class MockC99BareMetalBackend final : public fsm::backend::ICodegenBackend {
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "c99"; }
    [[nodiscard]] fsm::ir::TargetCapabilities capabilities() const noexcept override {
        return fsm::ir::TargetCapabilities::c99_baremetal();
    }
    [[nodiscard]] std::string emit(const fsm::ir::FsmIr& ir,
                                   fsm::diagnostic::DiagnosticEngine& /*diag*/) const override {
        std::string code = "/* C99 flat FSM: " + ir.name + " */\nenum State { ";
        for (const auto& s : ir.states) {
            code += s.name + ", ";
        }
        code += "};\n";
        return code;
    }
};

class MockRustNoStdBackend final : public fsm::backend::ICodegenBackend {
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "rust"; }
    [[nodiscard]] fsm::ir::TargetCapabilities capabilities() const noexcept override {
        return fsm::ir::TargetCapabilities::rust_no_std();
    }
    [[nodiscard]] std::string emit(const fsm::ir::FsmIr& ir,
                                   fsm::diagnostic::DiagnosticEngine& /*diag*/) const override {
        return "#![no_std]\npub enum " + ir.name + "State {}\n";
    }
};
}  // namespace

/**
 * @brief Verify ICodegenBackend and TargetCapabilities-driven dynamic lowering for C++20, C99, and Rust no_std.
 * @scenario Run PassManager::create_lowering_pipeline(backend.capabilities()) for CppCodegenBackend,
 *           MockC99BareMetalBackend, and MockRustNoStdBackend on an IR with deferred events.
 * @expected C++20 preserves deferred events natively; C99 and Rust no_std dynamically schedule
 * DeferredEventLoweringPass and emit valid lowered output; C99 schedules ChoiceInliningPass whereas Rust no_std skips
 * it.
 */
TEST(EmitterFactoryTest, ICodegenBackend_TargetCapabilitiesDrivenLowering) {
    using namespace fsm::backend;
    using namespace fsm::middleend;

    // Build IR with deferred event
    FsmIr base_ir;
    base_ir.name = "TelemetryFsm";
    base_ir.initial_state = "Busy";
    StateNode busy("Busy");
    busy.deferred_events.push_back("EvConfig");
    base_ir.add_state(busy);
    base_ir.add_state("Ready");
    base_ir.add_transition(TransitionEdge("Busy", "Ready", "EvDone"));
    base_ir.add_transition(TransitionEdge("Ready", "Busy", "EvConfig"));

    // 1. C++20 backend preserves deferred events natively
    CppCodegenBackend cpp_backend;
    EXPECT_EQ(cpp_backend.id(), "cpp17");
    EXPECT_TRUE(cpp_backend.capabilities().supports_deferred);
    EXPECT_TRUE(cpp_backend.capabilities().supports_history);
    EXPECT_TRUE(cpp_backend.capabilities().supports_choice);
    EXPECT_FALSE(cpp_backend.capabilities().supports_orthogonal);

    FsmIr cpp_ir = base_ir;
    auto cpp_pm = PassManager::create_lowering_pipeline(cpp_backend.capabilities());
    fsm::diagnostic::DiagnosticEngine cpp_diag;
    EXPECT_TRUE(cpp_pm.run(cpp_ir, cpp_diag));
    const auto* busy_cpp = cpp_ir.find_state("Busy");
    ASSERT_NE(busy_cpp, nullptr);
    EXPECT_FALSE(busy_cpp->deferred_events.empty());
    EXPECT_FALSE(cpp_backend.emit(cpp_ir, cpp_diag).empty());

    // 2. Mock C99 bare-metal backend lowers deferred events and schedules choice inlining
    MockC99BareMetalBackend c99_backend;
    EXPECT_FALSE(c99_backend.capabilities().supports_deferred);
    EXPECT_FALSE(c99_backend.capabilities().supports_choice);
    FsmIr c99_ir = base_ir;
    auto c99_pm = PassManager::create_lowering_pipeline(c99_backend.capabilities());
    fsm::diagnostic::DiagnosticEngine c99_diag;
    EXPECT_TRUE(c99_pm.run(c99_ir, c99_diag));
    const auto* busy_c99 = c99_ir.find_state("Busy");
    ASSERT_NE(busy_c99, nullptr);
    EXPECT_TRUE(busy_c99->deferred_events.empty());
    const std::string c99_code = c99_backend.emit(c99_ir, c99_diag);
    EXPECT_NE(c99_code.find("C99 flat FSM: TelemetryFsm"), std::string::npos);

    // 3. Mock Rust no_std backend supports choice natively (fewer passes than C99) but lowers deferred events
    MockRustNoStdBackend rust_backend;
    EXPECT_TRUE(rust_backend.capabilities().supports_choice);
    EXPECT_FALSE(rust_backend.capabilities().supports_deferred);
    auto rust_pm = PassManager::create_lowering_pipeline(rust_backend.capabilities());
    EXPECT_LT(rust_pm.pass_count(), c99_pm.pass_count());
    FsmIr rust_ir = base_ir;
    fsm::diagnostic::DiagnosticEngine rust_diag;
    EXPECT_TRUE(rust_pm.run(rust_ir, rust_diag));
    EXPECT_NE(rust_backend.emit(rust_ir, rust_diag).find("#![no_std]"), std::string::npos);
}

}  // namespace

/**
 * @file codegen_backend.hpp
 * @brief Extensible ICodegenBackend contract pairing target emission with declarative TargetCapabilities.
 */

#pragma once

#include <string>
#include <string_view>

#include "fsm/backend/c99/c99_generator.hpp"
#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/cpp/cpp_options.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/target_capabilities.hpp"

namespace fsm::backend {

/**
 * @class ICodegenBackend
 * @brief Abstract contract for target language code generators (C++17/20, C99, Rust, custom backends).
 *
 * Each backend declares its native TargetCapabilities so the middle-end PassManager can lower
 * unsupported statechart constructs automatically before invoking emit().
 */
class ICodegenBackend {
  public:
    virtual ~ICodegenBackend() = default;

    /**
     * @brief Unique identifier of this backend (e.g. "cpp17", "cpp20", "c99", "rust").
     */
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;

    /**
     * @brief Declarative capabilities supported natively by this target backend.
     */
    [[nodiscard]] virtual ir::TargetCapabilities capabilities() const noexcept = 0;

    /**
     * @brief Emits target source code from a lowered, canonical FsmIr.
     * @param ir Lowered FsmIr satisfying this backend's TargetCapabilities.
     * @param diag Diagnostic engine for backend errors or warnings.
     * @return Generated source code string (empty on fatal error).
     */
    [[nodiscard]] virtual std::string emit(const ir::FsmIr& ir, diagnostic::DiagnosticEngine& diag) const = 0;
};

/**
 * @class CppCodegenBackend
 * @brief Built-in C++17/C++20 code generation backend wrapping CppGenerator.
 */
class CppCodegenBackend final : public ICodegenBackend {
  public:
    explicit CppCodegenBackend(cpp::GeneratorOptions options = {}) : options_(std::move(options)) {}

    [[nodiscard]] std::string_view id() const noexcept override { return "cpp17"; }

    [[nodiscard]] ir::TargetCapabilities capabilities() const noexcept override {
        return ir::TargetCapabilities::cpp17();
    }

    [[nodiscard]] std::string emit(const ir::FsmIr& ir, diagnostic::DiagnosticEngine& diag) const override {
        try {
            return cpp::CppGenerator::generate_header(ir, options_);
        } catch (const std::exception& ex) {
            diag.report(diagnostic::Diagnostic::error("E_CPP_CODEGEN", ex.what()));
            return {};
        }
    }

  private:
    cpp::GeneratorOptions options_;
};

/**
 * @class C99CodegenBackend
 * @brief Native Pure-C99 bare-metal code generation backend wrapping C99Generator.
 */
class C99CodegenBackend final : public ICodegenBackend {
  public:
    explicit C99CodegenBackend(c99::C99GeneratorOptions options = {}) : options_(std::move(options)) {}

    [[nodiscard]] std::string_view id() const noexcept override { return "c99"; }

    [[nodiscard]] ir::TargetCapabilities capabilities() const noexcept override {
        return ir::TargetCapabilities::c99_baremetal();
    }

    [[nodiscard]] std::string emit(const ir::FsmIr& ir, diagnostic::DiagnosticEngine& diag) const override {
        try {
            const auto bundle = c99::C99Generator::generate(ir, options_);
            return bundle.header + "\n/* === IMPLEMENTATION === */\n" + bundle.source;
        } catch (const std::exception& ex) {
            diag.report(diagnostic::Diagnostic::error("E_C99_CODEGEN", ex.what()));
            return {};
        }
    }

    [[nodiscard]] c99::GeneratedC99Bundle emit_bundle(const ir::FsmIr& ir, diagnostic::DiagnosticEngine& diag) const {
        try {
            return c99::C99Generator::generate(ir, options_);
        } catch (const std::exception& ex) {
            diag.report(diagnostic::Diagnostic::error("E_C99_CODEGEN", ex.what()));
            return {};
        }
    }

  private:
    c99::C99GeneratorOptions options_;
};

}  // namespace fsm::backend

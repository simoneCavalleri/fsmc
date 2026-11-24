/**
 * @file pass_interface.hpp
 * @brief Core middle-end pass interface, telemetry stats, and registry descriptor types.
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend {

/**
 * @struct PassExecutionStats
 * @brief Telemetry measurements captured during pass pipeline execution.
 */
struct PassExecutionStats {
    std::string pass_name;    ///< Unique identifier of the pass
    double duration_ms{0.0};  ///< Execution duration in milliseconds
    bool modified_ir{false};  ///< True if IR was mutated by this pass
};

/**
 * @class IPass
 * @brief Abstract interface for middle-end analysis, validation, and optimization passes.
 */
class IPass {
  public:
    virtual ~IPass() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::string description() const = 0;
    virtual bool run(ir::FsmIr& ir, diagnostic::DiagnosticEngine& diag) = 0;
    [[nodiscard]] virtual std::vector<std::string> required_prerequisites() const { return {}; }
    [[nodiscard]] virtual std::vector<std::string> invalidated_analyses() const { return {}; }
    [[nodiscard]] virtual bool preserves_ir() const noexcept { return false; }
};

/**
 * @struct PassDescriptor
 * @brief Metadata and factory descriptor for a built-in pass registered in PassRegistry.
 */
struct PassDescriptor {
    std::string_view canonical_name;
    std::vector<std::string_view> aliases;
    std::string_view category;
    std::string_view description;
    bool preserves_ir{false};
    std::function<std::unique_ptr<IPass>()> factory;
};

/**
 * @class PassRegistry
 * @brief Central registry of built-in middle-end passes (single source of truth for PassManager & CLI).
 */
class PassRegistry {
  public:
    using SubmachineResolver = std::function<const ir::FsmIr*(const std::string&)>;

    [[nodiscard]] static const std::vector<PassDescriptor>& descriptors();
    [[nodiscard]] static std::vector<std::string_view> registered_names();
    [[nodiscard]] static std::unique_ptr<IPass> create(std::string_view pass_name, std::string_view pipe_cmd = "",
                                                       SubmachineResolver submachine_resolver = nullptr);
};

}  // namespace fsm::middleend

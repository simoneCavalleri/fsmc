/**
 * @file pass_manager.hpp
 * @brief Pipeline coordinator, built-in analysis passes, and pass registry integration.
 */

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/target_capabilities.hpp"
#include "fsm/middleend/analysis_manager.hpp"
#include "fsm/middleend/pass_interface.hpp"
#include "fsm/middleend/plugin/plugin_loader.hpp"

namespace fsm::middleend {

// ============================================================================
// Core Pipeline Passes (Direct IPass Implementations)
// ============================================================================

class HierarchyCanonicalizationPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(ir::FsmIr& ir, diagnostic::DiagnosticEngine& diag) override;
};

class SemanticValidationPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(ir::FsmIr& ir, diagnostic::DiagnosticEngine& diag) override;
    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }
};

class ChoiceCompletenessPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(ir::FsmIr& ir, diagnostic::DiagnosticEngine& diag) override;
    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }
};

class EFSMDataPathPass : public IPass {
  public:
    explicit EFSMDataPathPass(bool prune_dead_transitions = false);
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(ir::FsmIr& ir, diagnostic::DiagnosticEngine& diag) override;
    [[nodiscard]] bool preserves_ir() const noexcept override { return !prune_dead_transitions_; }

  private:
    bool prune_dead_transitions_{false};
};

class ModelSafetyVerifierPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(ir::FsmIr& ir, diagnostic::DiagnosticEngine& diag) override;
    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }
};

class ModelCheckingPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(ir::FsmIr& ir, diagnostic::DiagnosticEngine& diag) override;
    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }
};

// ============================================================================
// PassManager: Pipeline Coordinator
// ============================================================================

/**
 * @class PassManager
 * @brief Coordinates sequential execution of middle-end compiler passes and plugins.
 */
class PassManager {
  public:
    PassManager();
    ~PassManager();
    PassManager(PassManager&&) noexcept;
    PassManager& operator=(PassManager&&) noexcept;

    [[nodiscard]] static PassManager create_default_pipeline();
    [[nodiscard]] static PassManager create_optimizing_pipeline(bool prune_dead_states = true,
                                                                bool minimize_states = false);
    [[nodiscard]] static PassManager create_verified_7stage_pipeline(bool optimize = true);
    [[nodiscard]] static PassManager create_validation_pipeline();
    [[nodiscard]] static PassManager create_lowering_pipeline(const ir::TargetCapabilities& caps);

    void add_lowering_passes_for(const ir::TargetCapabilities& caps);
    void add_pass(std::unique_ptr<IPass> pass);
    [[nodiscard]] std::size_t pass_count() const noexcept;
    void truncate_passes(std::size_t count) noexcept;

    bool load_plugin(const std::string& plugin_path, diagnostic::DiagnosticEngine& diag);
    bool run(ir::FsmIr& ir, diagnostic::DiagnosticEngine& diag);

    void set_verify_ir_each_pass(bool enabled) noexcept { verify_ir_each_pass_ = enabled; }
    [[nodiscard]] bool verify_ir_each_pass() const noexcept { return verify_ir_each_pass_; }
    [[nodiscard]] std::string dump_pipeline() const;

    [[nodiscard]] const std::vector<PassExecutionStats>& get_stats() const noexcept;

    [[nodiscard]] AnalysisManager& analysis_manager() noexcept { return analysis_manager_; }
    [[nodiscard]] const AnalysisManager& analysis_manager() const noexcept { return analysis_manager_; }

    [[nodiscard]] static std::unique_ptr<IPass> create_pass_by_name(
        std::string_view pass_name, std::string_view pipe_cmd = "",
        PassRegistry::SubmachineResolver submachine_resolver = nullptr);

    [[nodiscard]] static std::vector<std::string_view> registered_pass_names();

  private:
    plugin::PluginLoader plugin_loader_;
    std::vector<std::unique_ptr<IPass>> passes_;
    std::vector<PassExecutionStats> stats_;
    AnalysisManager analysis_manager_;
    bool verify_ir_each_pass_{false};
};

}  // namespace fsm::middleend

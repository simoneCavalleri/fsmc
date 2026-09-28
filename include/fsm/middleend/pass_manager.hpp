/**
 * @file pass_manager.hpp
 * @brief Pipeline coordinator, pass interface, and built-in pass wrappers.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/plugin/plugin_loader.hpp"

namespace fsm::middleend::analysis {}

namespace fsm::middleend::passes {
class DeadStatePruningPass;
class WcetAnalysisPass;
class PipeThroughPass;
}  // namespace fsm::middleend::passes

namespace fsm::middleend {

using namespace diagnostic;
using namespace ir;

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

    /**
     * @brief Unique human-readable name of the pass.
     */
    [[nodiscard]] virtual std::string name() const = 0;

    /**
     * @brief Detailed explanation of pass behavior.
     */
    [[nodiscard]] virtual std::string description() const = 0;

    /**
     * @brief Executes the pass on the given FsmIr model.
     * @param ir FsmIr model to inspect or mutate.
     * @param diag Diagnostic engine for error/warning reporting.
     * @return True if pass succeeded without fatal errors, false otherwise.
     */
    virtual bool run(FsmIr& ir, DiagnosticEngine& diag) = 0;

    /**
     * @brief Required passes or analyses that must have executed successfully prior to this pass.
     */
    [[nodiscard]] virtual std::vector<std::string> required_prerequisites() const { return {}; }

    /**
     * @brief Names of analyses or assumptions invalidated by mutations performed in this pass.
     */
    [[nodiscard]] virtual std::vector<std::string> invalidated_analyses() const { return {}; }

    /**
     * @brief Indicates whether this pass is purely analytical (preserving IR structure).
     */
    [[nodiscard]] virtual bool preserves_ir() const noexcept { return false; }
};

// ============================================================================
// Concrete Passes & Wrappers
// ============================================================================

class HierarchyCanonicalizationPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class SemanticValidationPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class GuardSimplificationPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class GuardSatisfiabilityPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class DeterminismEnforcementPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class OrthogonalInterferencePassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class ChoiceCompletenessPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class ChoiceInliningPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class ModelSafetyVerifierPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class DeadStatePruningPassWrapper : public IPass {
  public:
    explicit DeadStatePruningPassWrapper(bool enable_pruning = true);
    ~DeadStatePruningPassWrapper() override;
    DeadStatePruningPassWrapper(DeadStatePruningPassWrapper&&) noexcept = default;
    DeadStatePruningPassWrapper& operator=(DeadStatePruningPassWrapper&&) noexcept = default;

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;

  private:
    std::unique_ptr<passes::DeadStatePruningPass> pass_;
};

class ModelCheckingPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class TimedDeadlockPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class EFSMDataPathPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class OrthogonalProductPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class WcetAnalysisPassWrapper : public IPass {
  public:
    explicit WcetAnalysisPassWrapper(std::size_t max_microstep_threshold = 100);
    ~WcetAnalysisPassWrapper() override;
    WcetAnalysisPassWrapper(WcetAnalysisPassWrapper&&) noexcept = default;
    WcetAnalysisPassWrapper& operator=(WcetAnalysisPassWrapper&&) noexcept = default;

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;

  private:
    std::unique_ptr<passes::WcetAnalysisPass> pass_;
};

class ConstantFoldingPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class StateMinimizationPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class PipeThroughPassWrapper : public IPass {
  public:
    explicit PipeThroughPassWrapper(std::string cmd);
    ~PipeThroughPassWrapper() override;
    PipeThroughPassWrapper(PipeThroughPassWrapper&&) noexcept = default;
    PipeThroughPassWrapper& operator=(PipeThroughPassWrapper&&) noexcept = default;

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;

  private:
    std::unique_ptr<passes::PipeThroughPass> pass_;
};

// --- Structural Lowering Wrappers ---

class ForkJoinLoweringPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class HistoryLoweringPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class DeferredEventLoweringPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class BoundaryActionFusionPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

// --- Data-Path Optimization Wrappers ---

class DeadActionEliminationPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class RegisterLivenessPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class TransitionFusionPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

class CommonActionFactoringPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

// --- Formal Verification Wrappers ---

class LivelockAnalysisPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }
};

class PriorityConflictPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }
};

class TimedInvariantsVerifierPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }
};

class EventQueueBoundPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
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

    /**
     * @brief Constructs the standard middle-end validation and canonicalization pipeline.
     */
    static PassManager create_default_pipeline();

    /**
     * @brief Constructs an optimizing middle-end pipeline with pruning and minimization.
     * @param prune_dead_states Enable removal of unreachable states and dead transitions.
     * @param minimize_states Enable DFA state minimization (Hopcroft/Moore).
     */
    static PassManager create_optimizing_pipeline(bool prune_dead_states = true, bool minimize_states = false);

    /**
     * @brief Constructs the comprehensive, verified 7-stage compilation pipeline.
     *
     * Stages:
     * 1. Frontend Canonicalization & Syntax Desugaring
     * 2. Structural Lowering Suite (Fork/Join, History, Deferred Events, Action Fusion)
     * 3. Formal Invariant & Safety Verification (Livelock, Priority, Timed Invariants, Event Queue)
     * 4. Symbolic Model Checking (Temporal logic, timed deadlocks)
     * 5. Optimization & Minimization (Dead code, Constant folding, Transition fusion, Minimization)
     * 6. Data-Path Optimization & Register Allocation
     * 7. Backend Preparation & Handoff (Determinism, WCET)
     */
    static PassManager create_verified_7stage_pipeline(bool optimize = true);

    /**
     * @brief Appends a custom or third-party pass to the execution queue.
     */
    void add_pass(std::unique_ptr<IPass> pass);

    /**
     * @brief Dynamically loads a pass plugin shared object (.so/.dylib).
     */
    bool load_plugin(const std::string& plugin_path, DiagnosticEngine& diag);

    /**
     * @brief Executes all registered passes sequentially on the IR model.
     * @param ir FsmIr model to process.
     * @param diag Diagnostic engine for errors and warnings.
     * @return True if all passes completed successfully without fatal errors, false otherwise.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);

    /**
     * @brief Retrieves timing and mutation telemetry from the most recent run.
     */
    [[nodiscard]] const std::vector<PassExecutionStats>& get_stats() const noexcept;

  private:
    plugin::PluginLoader plugin_loader_;
    std::vector<std::unique_ptr<IPass>> passes_;
    std::vector<PassExecutionStats> stats_;
};

}  // namespace fsm::middleend

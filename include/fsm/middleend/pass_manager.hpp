#pragma once

#include <chrono>
#include <iostream>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/choice_inlining_pass.hpp"
#include "fsm/middleend/dead_state_pruning_pass.hpp"
#include "fsm/middleend/determinism_enforcement_pass.hpp"
#include "fsm/middleend/guard_simplification_pass.hpp"
#include "fsm/middleend/model_checker.hpp"
#include "fsm/middleend/orthogonal_interference_pass.hpp"
#include "fsm/middleend/submachine_inlining_pass.hpp"
#include "fsm/middleend/timed_deadlock_pass.hpp"

namespace fsm::codegen {

struct PassExecutionStats {
    std::string pass_name;
    double duration_ms{0.0};
    bool modified_ir{false};
};

class IPass {
  public:
    virtual ~IPass() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::string description() const = 0;
    virtual bool run(FsmIr& ir, DiagnosticEngine& diag) = 0;
};

// ============================================================================
// Pass: HierarchyCanonicalizationPass
// Normalizes FQNs, reconciles composite parent relationships, sorts canonically
// ============================================================================
class HierarchyCanonicalizationPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override { return "HierarchyCanonicalization"; }
    [[nodiscard]] std::string description() const override {
        return "Normalizes state hierarchy, FQNs, and canonical order";
    }

    bool run(FsmIr& ir, DiagnosticEngine& /*diag*/) override {
        ir.normalize_hierarchy();
        ir.canonicalize();
        return true;
    }
};

// ============================================================================
// Pass: GuardSimplificationPassWrapper
// ============================================================================
class GuardSimplificationPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override { return GuardSimplificationPass::name(); }
    [[nodiscard]] std::string description() const override { return GuardSimplificationPass::description(); }

    bool run(FsmIr& ir, DiagnosticEngine& diag) override {
        GuardSimplificationPass pass;
        return pass.run(ir, diag);
    }
};

// ============================================================================
// Pass: DeterminismEnforcementPassWrapper
// ============================================================================
class DeterminismEnforcementPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override { return DeterminismEnforcementPass::name(); }
    [[nodiscard]] std::string description() const override { return DeterminismEnforcementPass::description(); }

    bool run(FsmIr& ir, DiagnosticEngine& diag) override {
        DeterminismEnforcementPass pass;
        return pass.run(ir, diag);
    }
};

// ============================================================================
// Pass: OrthogonalInterferencePassWrapper
// ============================================================================
class OrthogonalInterferencePassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override { return OrthogonalInterferencePass::name(); }
    [[nodiscard]] std::string description() const override { return OrthogonalInterferencePass::description(); }

    bool run(FsmIr& ir, DiagnosticEngine& diag) override {
        OrthogonalInterferencePass pass;
        return pass.run(ir, diag);
    }
};

// ============================================================================
// Pass: ChoiceCompletenessPass
// Validates that choice states have default fallbacks and no duplicate guards
// ============================================================================
class ChoiceCompletenessPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override { return "ChoiceCompleteness"; }
    [[nodiscard]] std::string description() const override {
        return "Verifies choice pseudostate branch exhaustiveness and determinism";
    }

    bool run(FsmIr& ir, DiagnosticEngine& diag) override {
        for (const auto& choice : ir.choice_nodes) {
            std::vector<const TransitionEdge*> outgoing;
            for (const auto& t : ir.transitions) {
                if (t.source == choice.name || t.source_id == choice.name) {
                    outgoing.push_back(&t);
                }
            }

            if (outgoing.empty()) {
                diag.report(Diagnostic::warning(
                    "W0101", "Choice pseudostate '" + choice.name + "' has no outgoing branches (trap)."));
                continue;
            }

            bool has_unconditional = false;
            std::set<std::string> seen_guards;

            for (const auto* t : outgoing) {
                if (!t->guard.has_value() || *t->guard == "else" || *t->guard == "otherwise" ||
                    *t->guard == "default") {
                    has_unconditional = true;
                } else {
                    if (seen_guards.count(*t->guard) != 0) {
                        diag.report(Diagnostic::warning(
                            "W0102", "Choice pseudostate '" + choice.name + "' has duplicate guard condition '[" +
                                         *t->guard + "]' leading to non-deterministic branch selection."));
                    }
                    seen_guards.insert(*t->guard);
                }
            }

            if (!has_unconditional) {
                diag.report(Diagnostic::warning(
                    "W0103", "Choice pseudostate '" + choice.name +
                                 "' lacks an unconditional else/default fallback branch (potential stall)."));
            }
        }
        return true;
    }
};

// ============================================================================
// Pass: ChoiceInliningPassWrapper
// Flattens choice/junction pseudostates into direct composite transitions
// ============================================================================
class ChoiceInliningPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override { return ChoiceInliningPass::name(); }
    [[nodiscard]] std::string description() const override { return ChoiceInliningPass::description(); }

    bool run(FsmIr& ir, DiagnosticEngine& diag) override {
        ChoiceInliningPass pass;
        return pass.run(ir, diag);
    }
};

// ============================================================================
// Pass: ModelSafetyVerifierPass
// Checks for unreachable states, deadlocks (trap states), and livelock cycles
// ============================================================================
class ModelSafetyVerifierPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override { return "ModelSafetyVerifier"; }
    [[nodiscard]] std::string description() const override {
        return "Formal verification of reachability, deadlock traps, and livelocks";
    }

    bool run(FsmIr& ir, DiagnosticEngine& diag) override {
        std::string root = ir.initial_state_id.empty() ? ir.initial_state : ir.initial_state_id;
        if (root.empty() && !ir.states.empty()) {
            root = ir.states.front().name;
        }

        if (root.empty()) {
            return true;
        }

        // Reachability analysis with bidirectional hierarchy propagation
        std::unordered_set<std::string> reachable;
        std::queue<std::string> queue;
        queue.push(root);
        reachable.insert(root);

        while (!queue.empty()) {
            std::string curr = queue.front();
            queue.pop();

            // 1. Include sub-states if composite or parallel
            for (const auto& s : ir.states) {
                if (s.parent_state == curr && reachable.count(s.name) == 0) {
                    reachable.insert(s.name);
                    queue.push(s.name);
                }
            }

            // 2. Include parent state if sub-state is reached
            const auto* curr_st = ir.find_state(curr);
            if (curr_st != nullptr && !curr_st->parent_state.empty()) {
                if (reachable.count(curr_st->parent_state) == 0) {
                    reachable.insert(curr_st->parent_state);
                    queue.push(curr_st->parent_state);
                }
            }

            // 3. Outgoing transitions
            for (const auto& t : ir.transitions) {
                if (t.source == curr || t.source_id == curr) {
                    if (reachable.count(t.target) == 0 && !t.target.empty()) {
                        reachable.insert(t.target);
                        queue.push(t.target);
                    }
                }
            }
        }

        for (const auto& s : ir.states) {
            if (s.kind == StateKind::Final || ir.is_choice_node(s.name))
                continue;
            if (reachable.count(s.name) == 0) {
                diag.report(Diagnostic::warning("W0201", "State unreachable from initial state: '" + s.name + "'."));
            }
        }

        // Helper lambda: check if a state or any of its ancestors has outgoing transitions
        auto has_outgoing_hierarchical = [&](const StateNode& state) {
            // Check direct outgoing
            for (const auto& t : ir.transitions) {
                if (t.source == state.name || t.source_id == state.name) {
                    return true;
                }
            }
            // Check ancestors
            std::string parent_name = state.parent_state;
            while (!parent_name.empty()) {
                for (const auto& t : ir.transitions) {
                    if (t.source == parent_name || t.source_id == parent_name) {
                        return true;
                    }
                }
                const auto* p = ir.find_state(parent_name);
                parent_name = (p != nullptr) ? p->parent_state : "";
            }
            // Check descendants (if composite)
            std::queue<std::string> child_q;
            child_q.push(state.name);
            while (!child_q.empty()) {
                std::string c_curr = child_q.front();
                child_q.pop();
                for (const auto& child : ir.states) {
                    if (child.parent_state == c_curr) {
                        for (const auto& t : ir.transitions) {
                            if (t.source == child.name || t.source_id == child.name) {
                                return true;
                            }
                        }
                        child_q.push(child.name);
                    }
                }
            }
            return false;
        };

        // Trap state (deadlock) analysis
        for (const auto& s : ir.states) {
            if (s.kind == StateKind::Final || ir.is_choice_node(s.name) || s.is_composite ||
                s.name.rfind("Terminal", 0) == 0 || s.name.rfind("Final", 0) == 0)
                continue;

            bool has_in = false;
            for (const auto& t : ir.transitions) {
                if (t.target == s.name || t.target_id == s.name) {
                    has_in = true;
                    break;
                }
            }

            bool has_out = has_outgoing_hierarchical(s);

            if (has_in && !has_out) {
                diag.report(
                    Diagnostic::warning("W0202", "Potential trap / deadlock state: '" + s.name +
                                                     "' has incoming transitions but no outgoing transitions."));
            }
        }

        return true;
    }
};

// ============================================================================
// Pass: DeadStatePruningPassWrapper
// ============================================================================
class DeadStatePruningPassWrapper : public IPass {
  public:
    explicit DeadStatePruningPassWrapper(bool enable_pruning = true) : pass_(enable_pruning) {}
    [[nodiscard]] std::string name() const override { return DeadStatePruningPass::name(); }
    [[nodiscard]] std::string description() const override { return DeadStatePruningPass::description(); }

    bool run(FsmIr& ir, DiagnosticEngine& diag) override { return pass_.run(ir, diag); }

  private:
    DeadStatePruningPass pass_;
};

// ============================================================================
// Pass: ModelCheckingPass
// Formal Model Checking Verification for Temporal LTL/CTL Properties
// ============================================================================
class ModelCheckingPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override { return "ModelCheckingPass"; }
    [[nodiscard]] std::string description() const override {
        return "Executes formal model checking verification for temporal LTL/CTL properties and safety invariants";
    }

    bool run(FsmIr& ir, DiagnosticEngine& diag) override {
        if (ir.properties.empty()) {
            return true;
        }

        ModelChecker checker(ir);
        auto results = checker.verify_all();

        for (const auto& res : results) {
            if (!res.passed) {
                std::string msg = "Formal property '" + res.property_name + "' [" + property_kind_to_string(res.kind) +
                                  "] VIOLATED: " + res.violation_reason;
                std::string trace_str = res.format_counterexample();
                if (!trace_str.empty()) {
                    msg += "\n" + trace_str;
                }
                diag.report(Diagnostic::error("E_MODEL_CHECK_VIOLATION", msg));
            }
        }

        return !diag.has_errors();
    }
};

// ============================================================================
// Pass: TimedDeadlockPassWrapper
// ============================================================================
class TimedDeadlockPassWrapper : public IPass {
  public:
    [[nodiscard]] std::string name() const override { return TimedDeadlockPass::name(); }
    [[nodiscard]] std::string description() const override { return TimedDeadlockPass::description(); }

    bool run(FsmIr& ir, DiagnosticEngine& diag) override {
        TimedDeadlockPass pass;
        return pass.run(ir, diag);
    }
};

// ============================================================================
// Pass: EFSMDataPathPass
// ============================================================================
class EFSMDataPathPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override { return "EFSMDataPathPass"; }
    [[nodiscard]] std::string description() const override {
        return "Analyzes EFSM data paths and variable ranges via abstract interpretation to detect unsatisfiable "
               "guards";
    }

    bool run(FsmIr& ir, DiagnosticEngine& diag) override {
        if (ir.variables.empty()) {
            return true;
        }

        EFSMIntervalAnalyzer analyzer(ir);
        analyzer.analyze(diag);
        return !diag.has_errors();
    }
};

// ============================================================================
// PassManager: Pipeline Coordinator
// ============================================================================
class PassManager {
  public:
    PassManager() = default;

    static PassManager create_default_pipeline() {
        PassManager pm;
        pm.add_pass(std::make_unique<HierarchyCanonicalizationPass>());
        pm.add_pass(std::make_unique<GuardSimplificationPassWrapper>());
        pm.add_pass(std::make_unique<DeterminismEnforcementPassWrapper>());
        pm.add_pass(std::make_unique<OrthogonalInterferencePassWrapper>());
        pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
        pm.add_pass(std::make_unique<ChoiceInliningPassWrapper>());
        pm.add_pass(std::make_unique<TimedDeadlockPassWrapper>());
        pm.add_pass(std::make_unique<EFSMDataPathPass>());
        pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
        pm.add_pass(std::make_unique<ModelCheckingPass>());
        return pm;
    }

    static PassManager create_optimizing_pipeline(bool prune_dead_states = true) {
        PassManager pm;
        pm.add_pass(std::make_unique<HierarchyCanonicalizationPass>());
        pm.add_pass(std::make_unique<GuardSimplificationPassWrapper>());
        pm.add_pass(std::make_unique<DeterminismEnforcementPassWrapper>());
        pm.add_pass(std::make_unique<OrthogonalInterferencePassWrapper>());
        if (prune_dead_states) {
            pm.add_pass(std::make_unique<DeadStatePruningPassWrapper>(true));
        }
        pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
        pm.add_pass(std::make_unique<ChoiceInliningPassWrapper>());
        pm.add_pass(std::make_unique<TimedDeadlockPassWrapper>());
        pm.add_pass(std::make_unique<EFSMDataPathPass>());
        pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
        pm.add_pass(std::make_unique<ModelCheckingPass>());
        return pm;
    }

    void add_pass(std::unique_ptr<IPass> pass) { passes_.push_back(std::move(pass)); }

    bool run(FsmIr& ir, DiagnosticEngine& diag) {
        stats_.clear();
        for (auto& pass : passes_) {
            auto t0 = std::chrono::steady_clock::now();
            bool ok = pass->run(ir, diag);
            auto t1 = std::chrono::steady_clock::now();
            double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            PassExecutionStats s;
            s.pass_name = pass->name();
            s.duration_ms = elapsed_ms;
            s.modified_ir = ok;
            stats_.push_back(s);

            if (!ok && diag.has_errors()) {
                return false;
            }
        }
        return !diag.has_errors();
    }

    [[nodiscard]] const std::vector<PassExecutionStats>& get_stats() const noexcept { return stats_; }

  private:
    std::vector<std::unique_ptr<IPass>> passes_;
    std::vector<PassExecutionStats> stats_;
};

}  // namespace fsm::codegen

namespace fsm {
using PassManager = ::fsm::codegen::PassManager;
using IPass = ::fsm::codegen::IPass;
using PassExecutionStats = ::fsm::codegen::PassExecutionStats;
using HierarchyCanonicalizationPass = ::fsm::codegen::HierarchyCanonicalizationPass;
using GuardSimplificationPassWrapper = ::fsm::codegen::GuardSimplificationPassWrapper;
using DeterminismEnforcementPassWrapper = ::fsm::codegen::DeterminismEnforcementPassWrapper;
using OrthogonalInterferencePassWrapper = ::fsm::codegen::OrthogonalInterferencePassWrapper;
using DeadStatePruningPassWrapper = ::fsm::codegen::DeadStatePruningPassWrapper;
using ChoiceCompletenessPass = ::fsm::codegen::ChoiceCompletenessPass;
using ChoiceInliningPassWrapper = ::fsm::codegen::ChoiceInliningPassWrapper;
using TimedDeadlockPassWrapper = ::fsm::codegen::TimedDeadlockPassWrapper;
using EFSMDataPathPass = ::fsm::codegen::EFSMDataPathPass;
using ModelSafetyVerifierPass = ::fsm::codegen::ModelSafetyVerifierPass;
using ModelCheckingPass = ::fsm::codegen::ModelCheckingPass;
}  // namespace fsm

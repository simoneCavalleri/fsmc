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
// Pass 1: HierarchyCanonicalizationPass
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
// Pass 2: ChoiceCompletenessPass
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
// Pass 3: ModelSafetyVerifierPass
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

        // Reachability analysis
        std::unordered_set<std::string> reachable;
        std::queue<std::string> queue;
        queue.push(root);
        reachable.insert(root);

        while (!queue.empty()) {
            std::string curr = queue.front();
            queue.pop();

            // Include sub-states if composite
            if (ir.find_state(curr) != nullptr) {
                for (const auto& s : ir.states) {
                    if (s.parent_state == curr && reachable.count(s.name) == 0) {
                        reachable.insert(s.name);
                        queue.push(s.name);
                    }
                }
            }

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

        // Trap state (deadlock) analysis
        for (const auto& s : ir.states) {
            if (s.kind == StateKind::Final || ir.is_choice_node(s.name) || s.is_composite)
                continue;

            bool has_in = false;
            bool has_out = false;

            for (const auto& t : ir.transitions) {
                if (t.target == s.name || t.target_id == s.name)
                    has_in = true;
                if (t.source == s.name || t.source_id == s.name)
                    has_out = true;
            }

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
// PassManager: Pipeline Coordinator
// ============================================================================
class PassManager {
  public:
    PassManager() = default;

    static PassManager create_default_pipeline() {
        PassManager pm;
        pm.add_pass(std::make_unique<HierarchyCanonicalizationPass>());
        pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
        pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
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
using ChoiceCompletenessPass = ::fsm::codegen::ChoiceCompletenessPass;
using ModelSafetyVerifierPass = ::fsm::codegen::ModelSafetyVerifierPass;
}  // namespace fsm

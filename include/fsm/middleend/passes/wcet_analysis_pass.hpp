#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

/**
 * @brief Formal Middle-End Pass: Worst-Case Execution Time (WCET) & Zeno-Cycle Analysis.
 *
 * Verifies bounded execution and real-time determinism:
 * 1. Statically detects Zeno-cycles (zero-time loops of eventless/immediate micro-steps).
 * 2. Computes the deterministic upper bound on micro-steps per macro-step.
 */
class WcetAnalysisPass {
  public:
    explicit WcetAnalysisPass(std::size_t max_microstep_threshold = 100) : threshold_(max_microstep_threshold) {}

    [[nodiscard]] static std::string name() { return "WcetAnalysis"; }
    [[nodiscard]] static std::string description() {
        return "Analyzes micro-step execution chains and detects zero-time Zeno-cycles";
    }

    [[nodiscard]] bool has_zeno_cycle() const noexcept { return has_zeno_cycle_; }
    [[nodiscard]] std::size_t max_micro_steps() const noexcept { return max_micro_steps_; }

    bool run(FsmIr& ir, DiagnosticEngine& diag) {
        has_zeno_cycle_ = false;
        max_micro_steps_ = 0;

        // Build directed adjacency list of eventless / micro-step transitions
        std::unordered_map<std::string, std::vector<std::string>> adj;
        for (const auto& s : ir.states) {
            adj[s.name] = {};
        }
        for (const auto& c : ir.choice_nodes) {
            adj[c.name] = {};
        }

        auto is_microstep_transition = [&](const TransitionEdge& t) {
            if (t.event.empty() || t.event == "completion_event" || t.event == "anonymous_event") {
                return true;
            }
            if (ir.is_choice_node(t.source)) {
                return true;
            }
            return false;
        };

        for (const auto& t : ir.transitions) {
            if (is_microstep_transition(t)) {
                if (!t.target.empty() && t.source != t.target) {
                    adj[t.source].push_back(t.target);
                } else if (t.source == t.target && !t.source.empty()) {
                    // Direct self-loop on eventless transition is an immediate Zeno cycle
                    has_zeno_cycle_ = true;
                    diag.report(Diagnostic::error("E_ZENO_CYCLE", "Direct eventless self-loop on state '" + t.source +
                                                                      "' constitutes an infinite Zeno-cycle."));
                }
            }
        }

        // Detect cycles in micro-step DAG using DFS (white-gray-black coloring)
        // 0: Unvisited, 1: Visiting (in current stack), 2: Visited
        std::unordered_map<std::string, int> color;
        std::vector<std::string> path_stack;

        std::function<void(const std::string&)> dfs_cycle = [&](const std::string& u) {
            color[u] = 1;
            path_stack.push_back(u);

            for (const auto& v : adj[u]) {
                if (color[v] == 1) {
                    has_zeno_cycle_ = true;
                    std::string cycle_str;
                    bool in_cycle = false;
                    for (const auto& node : path_stack) {
                        if (node == v)
                            in_cycle = true;
                        if (in_cycle) {
                            if (!cycle_str.empty())
                                cycle_str += " -> ";
                            cycle_str += node;
                        }
                    }
                    cycle_str += " -> " + v;
                    diag.report(Diagnostic::error(
                        "E_ZENO_CYCLE",
                        "Detected Zeno-cycle (infinite micro-step loop without time advance): " + cycle_str));
                } else if (color[v] == 0) {
                    dfs_cycle(v);
                }
            }

            path_stack.pop_back();
            color[u] = 2;
        };

        for (const auto& [node, _] : adj) {
            if (color[node] == 0) {
                dfs_cycle(node);
            }
        }

        // If no cycles, compute longest path (WCET micro-step bound) via dynamic programming
        if (!has_zeno_cycle_) {
            std::unordered_map<std::string, std::size_t> memo;
            std::function<std::size_t(const std::string&)> get_longest = [&](const std::string& u) -> std::size_t {
                if (memo.find(u) != memo.end()) {
                    return memo[u];
                }
                std::size_t max_child = 0;
                for (const auto& v : adj[u]) {
                    max_child = std::max(max_child, 1 + get_longest(v));
                }
                memo[u] = max_child;
                return max_child;
            };

            for (const auto& [node, _] : adj) {
                max_micro_steps_ = std::max(max_micro_steps_, get_longest(node));
            }

            if (max_micro_steps_ > threshold_) {
                diag.report(Diagnostic::warning(
                    "W_WCET_HIGH_MICROSTEPS",
                    "Longest chained micro-step sequence (" + std::to_string(max_micro_steps_) +
                        ") exceeds configured real-time latency threshold (" + std::to_string(threshold_) + ")."));
            } else {
                diag.report(Diagnostic::info("I_WCET_BOUND", "Certified maximum micro-step bound per macro-step: " +
                                                                 std::to_string(max_micro_steps_) + " steps."));
            }
        }

        return !has_zeno_cycle_;
    }

  private:
    std::size_t threshold_{100};
    bool has_zeno_cycle_{false};
    std::size_t max_micro_steps_{0};
};

}  // namespace fsm::codegen

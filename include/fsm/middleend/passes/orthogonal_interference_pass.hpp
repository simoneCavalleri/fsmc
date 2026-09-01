#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

/**
 * @brief Target-Agnostic Middle-End Pass: Static concurrency & data-race analysis for parallel (AND) orthogonal
 * regions.
 *
 * Checks that concurrent transitions and actions across orthogonal regions do not write
 * to the same EFSM state variables or shared context fields without synchronization.
 */
class OrthogonalInterferencePass {
  public:
    [[nodiscard]] static std::string name() { return "OrthogonalInterference"; }
    [[nodiscard]] static std::string description() {
        return "Performs static data-race analysis and interference detection across concurrent orthogonal regions";
    }

    bool run(const FsmIr& ir, DiagnosticEngine& diag) {
        for (const auto& state : ir.states) {
            if (state.kind != StateKind::Parallel && state.orthogonal_regions.size() < 2) {
                continue;
            }

            const auto& regions = state.orthogonal_regions;
            // Compare each pair of distinct orthogonal regions
            for (std::size_t i = 0; i < regions.size(); ++i) {
                for (std::size_t j = i + 1; j < regions.size(); ++j) {
                    const auto& reg_a = regions[i];
                    const auto& reg_b = regions[j];

                    std::unordered_set<std::string> states_a(reg_a.state_ids.begin(), reg_a.state_ids.end());
                    std::unordered_set<std::string> states_b(reg_b.state_ids.begin(), reg_b.state_ids.end());

                    // Collect variables modified in region A
                    std::vector<std::pair<const TransitionEdge*, std::string>> writes_a;
                    for (const auto& t : ir.transitions) {
                        if (states_a.count(t.source) != 0 || states_a.count(t.source_id) != 0) {
                            if (t.action_sig.has_value()) {
                                for (const auto& assign : t.action_sig->assignments) {
                                    writes_a.emplace_back(&t, assign.target_variable);
                                }
                            }
                        }
                    }

                    // Collect variables modified in region B
                    std::vector<std::pair<const TransitionEdge*, std::string>> writes_b;
                    for (const auto& t : ir.transitions) {
                        if (states_b.count(t.source) != 0 || states_b.count(t.source_id) != 0) {
                            if (t.action_sig.has_value()) {
                                for (const auto& assign : t.action_sig->assignments) {
                                    writes_b.emplace_back(&t, assign.target_variable);
                                }
                            }
                        }
                    }

                    // Check for overlapping variable writes (data races)
                    for (const auto& [tr_a, var_a] : writes_a) {
                        for (const auto& [tr_b, var_b] : writes_b) {
                            if (var_a == var_b) {
                                std::string msg = "Data race / concurrent interference detected in parallel state '" +
                                                  state.name + "': Region '" + reg_a.name + "' (transition " +
                                                  tr_a->source + "->" + tr_a->target + ") and Region '" + reg_b.name +
                                                  "' (transition " + tr_b->source + "->" + tr_b->target +
                                                  ") concurrently mutate shared variable '" + var_a + "'.";
                                diag.report(Diagnostic::safety_critical("W_CONCURRENT_DATA_RACE", msg));
                            }
                        }
                    }
                }
            }
        }
        return true;
    }
};

}  // namespace fsm::codegen

namespace fsm {
using OrthogonalInterferencePass = ::fsm::codegen::OrthogonalInterferencePass;
}  // namespace fsm

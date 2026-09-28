#include "fsm/middleend/passes/orthogonal_interference_pass.hpp"

#include <unordered_set>
#include <utility>
#include <vector>

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

bool OrthogonalInterferencePass::run(const FsmIr& ir, DiagnosticEngine& diag) {
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
                    if (states_a.count(t.source) != 0) {
                        if (t.condition_action.has_value()) {
                            for (const auto& assign : t.condition_action->assignments) {
                                writes_a.emplace_back(&t, assign.target.name);
                            }
                        }
                        if (t.transition_action.has_value()) {
                            for (const auto& assign : t.transition_action->assignments) {
                                writes_a.emplace_back(&t, assign.target.name);
                            }
                        }
                    }
                }

                // Collect variables modified in region B
                std::vector<std::pair<const TransitionEdge*, std::string>> writes_b;
                for (const auto& t : ir.transitions) {
                    if (states_b.count(t.source) != 0) {
                        if (t.condition_action.has_value()) {
                            for (const auto& assign : t.condition_action->assignments) {
                                writes_b.emplace_back(&t, assign.target.name);
                            }
                        }
                        if (t.transition_action.has_value()) {
                            for (const auto& assign : t.transition_action->assignments) {
                                writes_b.emplace_back(&t, assign.target.name);
                            }
                        }
                    }
                }

                // Check for overlapping variable writes (data races or architectural isolation violations)
                for (const auto& [tr_a, var_a] : writes_a) {
                    for (const auto& [tr_b, var_b] : writes_b) {
                        if (var_a == var_b) {
                            if (ir.concurrency.datapath_isolation == DatapathIsolation::PartitionedDatapath) {
                                std::string msg =
                                    "Architectural isolation violation in PartitionedDatapath model: Parallel regions "
                                    "'" +
                                    reg_a.name + "' and '" + reg_b.name + "' both mutate context variable '" + var_a +
                                    "'. Partitioned datapath requires disjoint variables or signal exchange.";
                                diag.report(Diagnostic::safety_critical("E_PARTITIONED_DATAPATH_VIOLATION", msg));
                            } else if (ir.concurrency.orthogonal_conflict ==
                                           OrthogonalConflictResolution::PriorityOrdered &&
                                       reg_a.priority != reg_b.priority) {
                                // Deterministically serialized by explicit region priority
                                std::string msg = "Sequential write interference in PriorityOrdered parallel state '" +
                                                  state.name + "': Region '" + reg_a.name +
                                                  "' (prio=" + std::to_string(reg_a.priority) + ") and Region '" +
                                                  reg_b.name + "' (prio=" + std::to_string(reg_b.priority) +
                                                  ") access shared variable '" + var_a + "'. Serialized by priority.";
                                diag.report(Diagnostic::warning("W_PRIORITY_ORDERED_INTERFERENCE", msg));
                            } else if (ir.concurrency.orthogonal_conflict ==
                                       OrthogonalConflictResolution::DocumentOrder) {
                                // Deterministically serialized by SCXML document traversal order
                                std::string msg = "Sequential write in DocumentOrder parallel state '" + state.name +
                                                  "': Region '" + reg_a.name + "' and Region '" + reg_b.name +
                                                  "' access shared variable '" + var_a +
                                                  "'. Serialized by SCXML document order.";
                                diag.report(Diagnostic::info("I_DOCUMENT_ORDER_SERIALIZED", msg));
                            } else {
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
    }
    return true;
}

}  // namespace fsm::middleend::passes

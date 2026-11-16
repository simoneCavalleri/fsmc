#include "fsm/middleend/passes/orthogonal_interference_pass.hpp"

#include <unordered_set>
#include <utility>
#include <vector>

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

namespace {

struct RegionWrite {
    std::string location;
    std::string variable;
};

std::unordered_set<std::string> collect_region_states(const FsmIr& ir, const OrthogonalRegion& reg) {
    std::unordered_set<std::string> states;
    std::vector<std::string> queue;
    for (const auto& sid : reg.state_ids) {
        if (const auto* st = ir.find_state(sid)) {
            if (states.insert(st->name).second) {
                queue.push_back(st->name);
            }
        } else if (states.insert(sid).second) {
            queue.push_back(sid);
        }
    }
    if (!reg.name.empty() && states.insert(reg.name).second) {
        queue.push_back(reg.name);
    }
    if (!reg.id.empty() && reg.id != reg.name && states.insert(reg.id).second) {
        queue.push_back(reg.id);
    }
    for (std::size_t idx = 0; idx < queue.size(); ++idx) {
        const std::string curr = queue[idx];
        for (const auto& candidate : ir.states) {
            if (candidate.parent_state == curr && states.insert(candidate.name).second) {
                queue.push_back(candidate.name);
            }
        }
    }
    return states;
}

std::vector<RegionWrite> collect_region_writes(const FsmIr& ir, const OrthogonalRegion& reg) {
    const std::unordered_set<std::string> region_states = collect_region_states(ir, reg);
    std::vector<RegionWrite> writes;

    for (const auto& t : ir.transitions) {
        if (region_states.count(t.source) == 0) {
            continue;
        }
        const std::string loc = "transition " + t.source + "->" + t.target;
        if (t.condition_action.has_value()) {
            for (const auto& assign : t.condition_action->assignments) {
                writes.push_back({loc, assign.target.name});
            }
        }
        if (t.transition_action.has_value()) {
            for (const auto& assign : t.transition_action->assignments) {
                writes.push_back({loc, assign.target.name});
            }
        }
    }

    for (const auto& st : ir.states) {
        if (region_states.count(st.name) == 0) {
            continue;
        }
        for (const auto& act : st.entry_actions) {
            for (const auto& assign : act.assignments) {
                writes.push_back({"state " + st.name + " entry", assign.target.name});
            }
        }
        for (const auto& act : st.exit_actions) {
            for (const auto& assign : act.assignments) {
                writes.push_back({"state " + st.name + " exit", assign.target.name});
            }
        }
    }

    return writes;
}

}  // namespace

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

                const std::vector<RegionWrite> writes_a = collect_region_writes(ir, reg_a);
                const std::vector<RegionWrite> writes_b = collect_region_writes(ir, reg_b);

                // Check for overlapping variable writes (data races or architectural isolation violations)
                for (const auto& [loc_a, var_a] : writes_a) {
                    for (const auto& [loc_b, var_b] : writes_b) {
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
                                                  state.name + "': Region '" + reg_a.name + "' (" + loc_a +
                                                  ") and Region '" + reg_b.name + "' (" + loc_b +
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

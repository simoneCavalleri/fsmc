#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

/**
 * @brief Target-Agnostic Middle-End Pass: Orthogonal Region Cartesian Product Expansion.
 *
 * Expands concurrent parallel states and orthogonal regions (S_R1 x S_R2 x ... x S_Rn)
 * into an equivalent sequential hierarchical state space, eliminating runtime concurrency
 * and multi-threaded synchronization requirements.
 */
class OrthogonalProductPass {
  public:
    [[nodiscard]] static std::string name() { return "OrthogonalProduct"; }
    [[nodiscard]] static std::string description() {
        return "Computes Cartesian product of orthogonal regions into sequential product states";
    }

    bool run(FsmIr& ir, DiagnosticEngine& diag) {
        bool modified = false;

        // Iterate over states looking for parallel / orthogonal regions
        std::vector<std::string> parallel_state_names;
        for (const auto& s : ir.states) {
            if (s.kind == StateKind::Parallel || !s.orthogonal_regions.empty()) {
                parallel_state_names.push_back(s.name);
            }
        }

        for (const auto& p_name : parallel_state_names) {
            auto* parent = ir.find_state_mut(p_name);
            if (parent == nullptr) {
                continue;
            }

            if (parent->orthogonal_regions.size() < 2) {
                if (parent->kind == StateKind::Parallel) {
                    parent->kind = StateKind::Composite;
                    modified = true;
                }
                continue;
            }

            if (expand_parallel_state(ir, *parent, diag)) {
                modified = true;
            }
        }

        if (modified) {
            ir.normalize_hierarchy();
            ir.canonicalize();
        }

        return modified;
    }

  private:
    struct RegionInfo {
        std::string region_id;
        std::string initial_state;
        std::vector<std::string> states;
    };

    bool expand_parallel_state(FsmIr& ir, StateNode& parent, DiagnosticEngine& diag) {
        std::vector<RegionInfo> regions;
        std::unordered_set<std::string> all_sub_state_names;

        for (const auto& reg : parent.orthogonal_regions) {
            RegionInfo rinfo;
            rinfo.region_id = reg.name.empty() ? reg.id : reg.name;

            // 1. Discover member states
            for (const auto& sid : reg.state_ids) {
                const auto* st = ir.find_state(sid);
                if (st != nullptr) {
                    rinfo.states.push_back(st->name);
                } else {
                    rinfo.states.push_back(sid);
                }
            }

            // Also check states whose parent is this region or parent
            if (rinfo.states.empty()) {
                for (const auto& s : ir.states) {
                    if (s.parent_state == reg.id || s.parent_state == reg.name) {
                        rinfo.states.push_back(s.name);
                    }
                }
            }

            // Fallback: if region itself is a state node with children
            if (rinfo.states.empty()) {
                const auto* reg_node = ir.find_state(reg.id);
                if (reg_node != nullptr && !reg_node->children_ids.empty()) {
                    for (const auto& cid : reg_node->children_ids) {
                        const auto* ch = ir.find_state_by_id(cid);
                        if (ch != nullptr) {
                            rinfo.states.push_back(ch->name);
                        }
                    }
                }
            }

            // If still empty, use region itself as atomic state
            if (rinfo.states.empty()) {
                rinfo.states.push_back(reg.id.empty() ? reg.name : reg.id);
            }

            // Deduplicate states in region
            std::vector<std::string> unique_states;
            for (const auto& sn : rinfo.states) {
                if (std::find(unique_states.begin(), unique_states.end(), sn) == unique_states.end()) {
                    unique_states.push_back(sn);
                    all_sub_state_names.insert(sn);
                }
            }
            rinfo.states = std::move(unique_states);

            // Determine initial state
            if (!reg.initial_state_id.empty() &&
                std::find(rinfo.states.begin(), rinfo.states.end(), reg.initial_state_id) != rinfo.states.end()) {
                rinfo.initial_state = reg.initial_state_id;
            } else if (!rinfo.states.empty()) {
                rinfo.initial_state = rinfo.states.front();
            }

            regions.push_back(std::move(rinfo));
        }

        if (regions.size() < 2) {
            return false;
        }

        // 2. Cartesian product calculation
        std::vector<std::vector<std::string>> product_tuples;
        std::vector<std::string> current_tuple(regions.size());

        std::function<void(std::size_t)> generate_cartesian = [&](std::size_t reg_idx) {
            if (reg_idx == regions.size()) {
                product_tuples.push_back(current_tuple);
                return;
            }
            for (const auto& st : regions[reg_idx].states) {
                current_tuple[reg_idx] = st;
                generate_cartesian(reg_idx + 1);
            }
        };
        generate_cartesian(0);

        auto tuple_to_name = [&](const std::vector<std::string>& tup) -> std::string {
            std::string name = parent.name + "_";
            for (std::size_t i = 0; i < tup.size(); ++i) {
                if (i > 0)
                    name += "_";
                name += tup[i];
            }
            return name;
        };

        // Determine initial product tuple
        std::vector<std::string> init_tuple;
        for (const auto& reg : regions) {
            init_tuple.push_back(reg.initial_state);
        }
        std::string initial_product_state_name = tuple_to_name(init_tuple);

        // 3. Create product state nodes
        std::vector<StateNode> new_product_states;
        std::unordered_map<std::string, std::string> state_to_first_product;

        for (const auto& tup : product_tuples) {
            std::string s_name = tuple_to_name(tup);
            StateNode node(s_name, "Cartesian product state of " + parent.name, parent.name);
            node.kind = StateKind::Atomic;

            // Combine entry and exit actions from active member states
            for (const auto& member : tup) {
                const auto* m_node = ir.find_state(member);
                if (m_node != nullptr) {
                    for (const auto& act : m_node->entry_actions) {
                        node.entry_actions.push_back(act);
                    }
                    for (const auto& act : m_node->exit_actions) {
                        node.exit_actions.push_back(act);
                    }
                }
            }

            new_product_states.push_back(std::move(node));

            for (const auto& member : tup) {
                if (state_to_first_product.find(member) == state_to_first_product.end()) {
                    state_to_first_product[member] = s_name;
                }
            }
        }

        // 4. Synthesize transitions between product states
        std::vector<TransitionEdge> new_product_transitions;

        // Collect all intra-region transitions
        std::vector<TransitionEdge> intra_transitions;
        for (const auto& t : ir.transitions) {
            if (all_sub_state_names.count(t.source) > 0 || all_sub_state_names.count(t.target) > 0) {
                intra_transitions.push_back(t);
            }
        }

        // Distinct events occurring within regions
        std::set<std::string> region_events;
        for (const auto& t : intra_transitions) {
            if (!t.event.empty()) {
                region_events.insert(t.event);
            }
        }

        for (const auto& tup : product_tuples) {
            std::string src_product = tuple_to_name(tup);

            for (const auto& ev : region_events) {
                // Check which regions transition on ev from tup
                std::vector<std::string> next_tup = tup;
                bool any_transitioned = false;
                std::vector<std::string> guards;
                std::vector<std::string> actions;
                std::vector<ActionAssignment> assignments;

                for (std::size_t r = 0; r < regions.size(); ++r) {
                    const std::string& current_state = tup[r];
                    for (const auto& t : intra_transitions) {
                        if (t.source == current_state && t.event == ev) {
                            next_tup[r] = t.target;
                            any_transitioned = true;
                            if (t.guard.has_value() && !t.guard->empty()) {
                                guards.push_back(*t.guard);
                            }
                            if (t.action.has_value() && !t.action->empty()) {
                                actions.push_back(*t.action);
                            }
                            if (t.action_sig.has_value()) {
                                for (const auto& a : t.action_sig->assignments) {
                                    assignments.push_back(a);
                                }
                            }
                            break;
                        }
                    }
                }

                if (any_transitioned) {
                    std::string dst_product = tuple_to_name(next_tup);
                    TransitionEdge pt;
                    pt.source = src_product;
                    pt.target = dst_product;
                    pt.event = ev;
                    if (!guards.empty()) {
                        std::string combined_guard;
                        for (std::size_t i = 0; i < guards.size(); ++i) {
                            if (i > 0)
                                combined_guard += " && ";
                            combined_guard += "(" + guards[i] + ")";
                        }
                        pt.guard = combined_guard;
                    }
                    if (!actions.empty()) {
                        std::string combined_act;
                        for (std::size_t i = 0; i < actions.size(); ++i) {
                            if (i > 0)
                                combined_act += "; ";
                            combined_act += actions[i];
                        }
                        pt.action = combined_act;
                    }
                    if (!assignments.empty() || !actions.empty()) {
                        ActionSignature sig;
                        sig.name = pt.action.value_or("act_" + src_product + "_" + dst_product);
                        sig.assignments = assignments;
                        pt.action_sig = std::move(sig);
                    }
                    new_product_transitions.push_back(std::move(pt));
                }
            }
        }

        // 5. Remap external transitions
        // Remap incoming transitions targeting parent or sub-states
        for (auto& t : ir.transitions) {
            if (t.target == parent.name) {
                t.target = initial_product_state_name;
            } else if (all_sub_state_names.count(t.target) > 0) {
                auto it = state_to_first_product.find(t.target);
                if (it != state_to_first_product.end()) {
                    t.target = it->second;
                }
            }
        }

        // 6. Purge old member states and intra-transitions
        ir.states.erase(std::remove_if(ir.states.begin(), ir.states.end(),
                                       [&](const StateNode& s) {
                                           // Also purge region container states
                                           for (const auto& reg : parent.orthogonal_regions) {
                                               if (s.name == reg.id || s.name == reg.name)
                                                   return true;
                                           }
                                           return all_sub_state_names.count(s.name) > 0;
                                       }),
                        ir.states.end());

        ir.transitions.erase(std::remove_if(ir.transitions.begin(), ir.transitions.end(),
                                            [&](const TransitionEdge& t) {
                                                return all_sub_state_names.count(t.source) > 0 &&
                                                       all_sub_state_names.count(t.target) > 0;
                                            }),
                             ir.transitions.end());

        // 7. Inject new product states and transitions
        for (auto& ns : new_product_states) {
            ir.add_state(std::move(ns));
        }
        for (auto& nt : new_product_transitions) {
            ir.add_transition(std::move(nt));
        }

        // 8. Reclassify parent as Composite
        parent.kind = StateKind::Composite;
        parent.is_composite = true;
        parent.initial_sub_state = initial_product_state_name;
        parent.orthogonal_regions.clear();

        diag.report(Diagnostic::info("I_ORTHO_PRODUCT", "Expanded orthogonal parallel state '" + parent.name + "' (" +
                                                            std::to_string(regions.size()) + " regions) into " +
                                                            std::to_string(new_product_states.size()) +
                                                            " sequential product states."));

        return true;
    }
};

}  // namespace fsm::middleend::passes

namespace fsm::middleend {
using passes::OrthogonalProductPass;
}  // namespace fsm::middleend

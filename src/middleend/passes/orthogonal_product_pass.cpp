#include "fsm/middleend/passes/orthogonal_product_pass.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

std::string OrthogonalProductPass::name() {
    return "OrthogonalProduct";
}

std::string OrthogonalProductPass::description() {
    return "Computes Cartesian product of orthogonal regions into sequential product states";
}

bool OrthogonalProductPass::run(FsmIr& ir, DiagnosticEngine& diag) {
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
                diag.report(Diagnostic::error(
                    "EORTHO001", "parallel state '" + parent->name + "' requires at least two orthogonal regions"));
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

bool OrthogonalProductPass::expand_parallel_state(FsmIr& ir, StateNode& parent, DiagnosticEngine& diag) {
    const std::string parent_name = parent.name;
    const auto declared_regions = parent.orthogonal_regions;
    std::vector<RegionInfo> regions;
    std::unordered_set<std::string> all_sub_state_names;

    for (const auto& reg : declared_regions) {
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

    // Check product state space explosion bound
    std::size_t total_product_states = 1;
    for (const auto& reg : regions) {
        if (reg.states.empty()) {
            diag.report(Diagnostic::error(
                "EORTHO001", "region '" + reg.region_id + "' in parallel state '" + parent_name + "' has no states"));
            return false;
        }
        total_product_states *= reg.states.size();
        if (total_product_states > max_product_states) {
            diag.report(
                Diagnostic::error("EORTHO003", "parallel state '" + parent_name + "' Cartesian product state space (" +
                                                   std::to_string(total_product_states) + " states) exceeds bound (" +
                                                   std::to_string(max_product_states) + ")"));
            return false;
        }
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
        std::string name = parent_name + "_";
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
        StateNode node(s_name, "Cartesian product state of " + parent_name, parent_name);
        node.kind = StateKind::Atomic;
        // Mark this state so that normalize_hierarchy does not clear its
        // parent_state — the hierarchical relationship with the Parallel parent
        // is required for the runtime is_substate_of check.
        node.pinned_parent = true;

        // Combine entry and exit actions from active member states (deduplicated)
        for (const auto& member : tup) {
            const auto* m_node = ir.find_state(member);
            if (m_node != nullptr) {
                for (const auto& act : m_node->entry_actions) {
                    if (std::none_of(node.entry_actions.begin(), node.entry_actions.end(),
                                     [&](const auto& existing) { return existing.name == act.name; })) {
                        node.entry_actions.push_back(act);
                    }
                }
                for (const auto& act : m_node->exit_actions) {
                    if (std::none_of(node.exit_actions.begin(), node.exit_actions.end(),
                                     [&](const auto& existing) { return existing.name == act.name; })) {
                        node.exit_actions.push_back(act);
                    }
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
                        if (t.transition_action.has_value()) {
                            if (!t.transition_action->name.empty()) {
                                actions.push_back(t.transition_action->name);
                            }
                            for (const auto& a : t.transition_action->assignments) {
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
                if (!assignments.empty() || !actions.empty()) {
                    std::string combined_act;
                    for (std::size_t i = 0; i < actions.size(); ++i) {
                        if (i > 0)
                            combined_act += "; ";
                        combined_act += actions[i];
                    }
                    ActionSignature sig;
                    sig.name = combined_act.empty() ? ("act_" + src_product + "_" + dst_product) : combined_act;
                    sig.assignments = assignments;
                    pt.transition_action = std::move(sig);
                }
                new_product_transitions.push_back(std::move(pt));
            }
        }
    }

    // 5. Remap external transitions
    // A. Remap incoming transitions originating outside and targeting parent or sub-states (including multi-target from
    // Fork)
    for (auto& t : ir.transitions) {
        if (all_sub_state_names.count(t.source) > 0) {
            continue;
        }

        if (!t.target_ids.empty()) {
            bool targets_region = false;
            for (const auto& tid : t.target_ids) {
                if (all_sub_state_names.count(tid) > 0 || tid == parent_name) {
                    targets_region = true;
                    break;
                }
            }
            if (targets_region) {
                std::vector<std::string> resolved_tup(regions.size());
                for (std::size_t r = 0; r < regions.size(); ++r) {
                    resolved_tup[r] = regions[r].initial_state;
                    for (const auto& tid : t.target_ids) {
                        for (const auto& st : regions[r].states) {
                            if (st == tid) {
                                resolved_tup[r] = st;
                                break;
                            }
                        }
                    }
                }
                t.target = tuple_to_name(resolved_tup);
                t.target_ids.clear();
                t.multi_target_ids.clear();
            }
        } else if (t.target == parent_name) {
            t.target = initial_product_state_name;
        } else if (all_sub_state_names.count(t.target) > 0) {
            auto it = state_to_first_product.find(t.target);
            if (it != state_to_first_product.end()) {
                t.target = it->second;
            }
        }
    }

    // B. Remap outgoing transitions exiting parent or sub-states towards the outside (including multi-source from Join)
    for (auto& t : ir.transitions) {
        if (all_sub_state_names.count(t.target) > 0 || t.target == parent_name) {
            continue;
        }

        if (!t.source_ids.empty()) {
            bool sources_from_region = false;
            for (const auto& sid : t.source_ids) {
                if (all_sub_state_names.count(sid) > 0) {
                    sources_from_region = true;
                    break;
                }
            }
            if (sources_from_region) {
                std::vector<std::string> resolved_tup(regions.size());
                for (std::size_t r = 0; r < regions.size(); ++r) {
                    resolved_tup[r] = regions[r].states.empty() ? "" : regions[r].states.back();
                    for (const auto& sid : t.source_ids) {
                        for (const auto& st : regions[r].states) {
                            if (st == sid) {
                                resolved_tup[r] = st;
                                break;
                            }
                        }
                    }
                }
                t.source = tuple_to_name(resolved_tup);
                t.source_ids.clear();
                t.multi_source_ids.clear();
            }
        } else if (all_sub_state_names.count(t.source) > 0) {
            auto it = state_to_first_product.find(t.source);
            if (it != state_to_first_product.end()) {
                t.source = it->second;
            }
        }
    }

    // 6. Purge old member states and intra-transitions
    ir.states.erase(std::remove_if(ir.states.begin(), ir.states.end(),
                                   [&](const StateNode& s) {
                                       // Also purge region container states
                                       for (const auto& reg : declared_regions) {
                                           if (s.name == reg.id || s.name == reg.name)
                                               return true;
                                       }
                                       return all_sub_state_names.count(s.name) > 0;
                                   }),
                    ir.states.end());

    ir.transitions.erase(
        std::remove_if(ir.transitions.begin(), ir.transitions.end(),
                       [&](const TransitionEdge& t) { return all_sub_state_names.count(t.source) > 0; }),
        ir.transitions.end());

    // 7. Inject new product states and transitions
    for (auto& ns : new_product_states) {
        ir.add_state(std::move(ns));
    }
    for (auto& nt : new_product_transitions) {
        ir.add_transition(std::move(nt));
    }

    // 8. Reclassify parent as Composite after vector mutations have completed.
    auto* updated_parent = ir.find_state_mut(parent_name);
    if (updated_parent == nullptr) {
        diag.report(
            Diagnostic::error("EORTHO002", "parallel parent state '" + parent_name + "' disappeared during lowering"));
        return false;
    }
    updated_parent->kind = StateKind::Composite;
    updated_parent->is_composite = true;
    updated_parent->initial_sub_state = initial_product_state_name;
    updated_parent->orthogonal_regions.clear();

    diag.report(Diagnostic::info("I_ORTHO_PRODUCT", "Expanded orthogonal parallel state '" + parent_name + "' (" +
                                                        std::to_string(regions.size()) + " regions) into " +
                                                        std::to_string(new_product_states.size()) +
                                                        " sequential product states."));

    return true;
}

}  // namespace fsm::middleend::passes

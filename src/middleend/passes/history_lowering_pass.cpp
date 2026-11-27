#include "fsm/middleend/passes/history_lowering_pass.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "fsm/ir/action.hpp"
#include "fsm/ir/variable_definition.hpp"

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

bool HistoryLoweringPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    bool modified = false;

    // 1. Identify composite states with history
    std::vector<std::string> composite_with_history;
    for (const auto& s : ir.states) {
        if (s.has_history || s.has_deep_history || s.is_composite) {
            bool contains_history_node = false;
            for (const auto& child : ir.states) {
                if (child.parent_state == s.name &&
                    (child.kind == StateKind::ShallowHistory || child.kind == StateKind::DeepHistory)) {
                    contains_history_node = true;
                    break;
                }
            }
            if (s.has_history || s.has_deep_history || contains_history_node) {
                composite_with_history.push_back(s.name);
            }
        }
    }

    if (composite_with_history.empty()) {
        return false;
    }

    std::unordered_set<std::string> all_pruned_history_names;
    for (const auto& s : ir.states) {
        if (s.kind == StateKind::ShallowHistory || s.kind == StateKind::DeepHistory) {
            all_pruned_history_names.insert(s.name);
        }
    }

    for (const auto& parent_name : composite_with_history) {
        auto* parent = ir.find_state_mut(parent_name);
        if (parent == nullptr)
            continue;

        const bool parent_had_deep = parent->has_deep_history;
        parent->has_history = false;
        parent->has_deep_history = false;

        std::unordered_set<std::string> shallow_pseudos = {parent_name + ".[H]", parent_name + "_H"};
        std::unordered_set<std::string> deep_pseudos = {parent_name + ".[H*]", parent_name + "_HStar"};
        std::string base_default_substate =
            !parent->history_default_sub_state.empty() ? parent->history_default_sub_state : parent->initial_sub_state;

        bool has_deep_pseudo = false;
        for (const auto& s : ir.states) {
            if (s.parent_state == parent_name &&
                (s.kind == StateKind::ShallowHistory || s.kind == StateKind::DeepHistory)) {
                if (s.kind == StateKind::DeepHistory) {
                    deep_pseudos.insert(s.name);
                    has_deep_pseudo = true;
                } else {
                    shallow_pseudos.insert(s.name);
                }
                for (const auto& t : ir.transitions) {
                    if (t.source == s.name && !t.target.empty()) {
                        base_default_substate = t.target;
                    }
                }
            }
        }

        auto is_shallow_target = [&](const TransitionEdge& t) {
            if (shallow_pseudos.count(t.target) > 0) {
                return true;
            }
            return t.target == parent_name && t.target_is_history && !t.target_is_deep_history;
        };
        auto is_deep_target = [&](const TransitionEdge& t) {
            if (deep_pseudos.count(t.target) > 0) {
                return true;
            }
            return t.target == parent_name && t.target_is_deep_history;
        };

        bool needs_shallow = false;
        bool needs_deep = false;
        for (const auto& t : ir.transitions) {
            if (is_deep_target(t)) {
                needs_deep = true;
            } else if (is_shallow_target(t)) {
                needs_shallow = true;
            }
        }
        if (!needs_shallow && !needs_deep) {
            // Fallback for models where history was declared on StateNode without explicit incoming edge
            if (parent_had_deep || has_deep_pseudo) {
                needs_deep = true;
            } else {
                needs_shallow = true;
            }
        }

        const bool has_both = needs_shallow && needs_deep;

        auto lower_history_mode = [&](bool is_deep, bool use_deep_suffix) {
            const std::string history_var_name =
                use_deep_suffix ? ("__deep_history_state_" + parent_name) : ("__history_state_" + parent_name);
            const std::string restore_choice_name =
                use_deep_suffix ? (parent_name + "_DeepHistoryRestore") : (parent_name + "_HistoryRestore");
            const std::string grd_prefix =
                use_deep_suffix ? ("__dhist_" + parent_name + "_is_") : ("__hist_" + parent_name + "_is_");

            std::vector<std::string> target_substates;
            for (const auto& s : ir.states) {
                if (!is_deep && s.parent_state == parent_name && s.kind != StateKind::ShallowHistory &&
                    s.kind != StateKind::DeepHistory) {
                    target_substates.push_back(s.name);
                }
            }

            // Collect all direct substates (or deep leaf descendants) and assign deterministic integer indices
            for (auto& s : ir.states) {
                bool is_target = false;
                if (is_deep) {
                    std::string curr_p = s.parent_state;
                    std::unordered_set<std::string> visited;
                    while (!curr_p.empty() && visited.insert(curr_p).second) {
                        if (curr_p == parent_name) {
                            is_target = true;
                            break;
                        }
                        const auto* p_node = ir.find_state(curr_p);
                        curr_p = (p_node != nullptr) ? p_node->parent_state : "";
                    }
                } else {
                    is_target = (s.parent_state == parent_name);
                }

                if (is_target && s.kind != StateKind::ShallowHistory && s.kind != StateKind::DeepHistory &&
                    (!is_deep || !s.is_composite)) {
                    auto sub_it = std::find(target_substates.begin(), target_substates.end(), s.name);
                    std::size_t sub_idx = 0U;
                    if (sub_it == target_substates.end()) {
                        sub_idx = target_substates.size();
                        target_substates.push_back(s.name);
                    } else {
                        sub_idx = static_cast<std::size_t>(std::distance(target_substates.begin(), sub_it));
                    }
                    StoreOp store;
                    store.target = LValueTarget(history_var_name, LValueScope::Register);
                    store.op = AssignmentOp::Assign;
                    store.expression = std::to_string(sub_idx);

                    ActionSignature exit_sig((use_deep_suffix ? "__record_deep_history_" : "__record_history_") +
                                             s.name);
                    exit_sig.instructions.emplace_back(store, "Record active substate index on exit");
                    s.exit_actions.push_back(std::move(exit_sig));
                    modified = true;
                }
            }

            std::string default_substate = base_default_substate;
            if (is_deep && !default_substate.empty()) {
                std::unordered_set<std::string> visited_def;
                while (visited_def.insert(default_substate).second) {
                    const auto* def_node = ir.find_state(default_substate);
                    if (def_node != nullptr && def_node->is_composite && !def_node->initial_sub_state.empty()) {
                        default_substate = def_node->initial_sub_state;
                    } else {
                        break;
                    }
                }
            }
            if (default_substate.empty() && !target_substates.empty()) {
                default_substate = target_substates.front();
            }

            std::size_t default_idx = 0U;
            if (auto dit = std::find(target_substates.begin(), target_substates.end(), default_substate);
                dit != target_substates.end()) {
                default_idx = static_cast<std::size_t>(std::distance(target_substates.begin(), dit));
            }

            bool has_incoming = false;
            for (const auto& t : ir.transitions) {
                if ((is_deep && is_deep_target(t)) || (!is_deep && is_shallow_target(t))) {
                    has_incoming = true;
                    break;
                }
            }
            if (!has_incoming) {
                return;
            }

            bool var_exists = false;
            for (const auto& v : ir.variables) {
                if (v.name == history_var_name) {
                    var_exists = true;
                    break;
                }
            }
            if (!var_exists) {
                VariableDefinition vdef;
                vdef.name = history_var_name;
                vdef.type = "int32";
                vdef.initial_value = std::to_string(default_idx);
                vdef.description =
                    "Shadow register recording last active substate index of composite state " + parent_name;
                ir.variables.push_back(std::move(vdef));
                modified = true;
            }

            StateNode restore_node(restore_choice_name, "Choice branch restoring recorded history substate",
                                   parent_name);
            restore_node.kind = StateKind::Choice;
            restore_node.pinned_parent = true;
            ir.states.push_back(std::move(restore_node));
            ir.choice_nodes.emplace_back(restore_choice_name);

            for (std::size_t sub_idx = 0; sub_idx < target_substates.size(); ++sub_idx) {
                const auto& sub_name = target_substates[sub_idx];
                std::string grd_name = grd_prefix + sub_name;
                std::string grd_expr = history_var_name + " == " + std::to_string(sub_idx);
                ir.add_guard(grd_name, "History restore predicate for " + sub_name, grd_expr, grd_expr);
                TransitionEdge branch(restore_choice_name, sub_name, "", grd_name, std::nullopt,
                                      "Restore branch to " + sub_name, TransitionEdgeKind::External, 1);
                ir.transitions.push_back(std::move(branch));
            }

            TransitionEdge fallback(restore_choice_name, default_substate, "", "else", std::nullopt,
                                    "Default history restore fallback", TransitionEdgeKind::External, 2);
            ir.transitions.push_back(std::move(fallback));

            for (auto& t : ir.transitions) {
                const bool matches = is_deep ? is_deep_target(t) : is_shallow_target(t);
                if (matches) {
                    t.target = restore_choice_name;
                    t.target_id = compute_deterministic_id(restore_choice_name);
                    t.target_ids = {restore_choice_name};
                    t.multi_target_ids = {t.target_id};
                    t.target_is_history = false;
                    t.target_is_deep_history = false;
                    modified = true;
                }
            }
        };

        if (needs_shallow) {
            lower_history_mode(false, false);
        }
        if (needs_deep) {
            lower_history_mode(true, has_both);
        }
    }

    // 6. Prune obsolete History state nodes and their outgoing default transitions
    if (!all_pruned_history_names.empty()) {
        ir.transitions.erase(
            std::remove_if(ir.transitions.begin(), ir.transitions.end(),
                           [&](const TransitionEdge& t) { return all_pruned_history_names.count(t.source) > 0; }),
            ir.transitions.end());
    }
    ir.states.erase(std::remove_if(ir.states.begin(), ir.states.end(),
                                   [](const StateNode& s) {
                                       return s.kind == StateKind::ShallowHistory || s.kind == StateKind::DeepHistory;
                                   }),
                    ir.states.end());

    diag.report(
        Diagnostic::info("I_HISTORY_LOWERED", "Successfully lowered history pseudostates into shadow registers."));
    return modified;
}

}  // namespace fsm::middleend::passes

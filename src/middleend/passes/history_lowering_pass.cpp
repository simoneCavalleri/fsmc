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

    for (const auto& parent_name : composite_with_history) {
        auto* parent = ir.find_state_mut(parent_name);
        if (parent == nullptr)
            continue;

        bool is_deep = parent->has_deep_history;
        parent->has_history = false;
        parent->has_deep_history = false;
        std::string history_var_name = "__history_state_" + parent_name;

        // Collect all direct substates (or recursive substates if deep)
        std::vector<std::string> target_substates;
        std::string default_substate = parent->initial_sub_state;

        for (const auto& s : ir.states) {
            if (s.parent_state == parent_name) {
                if (s.kind == StateKind::ShallowHistory || s.kind == StateKind::DeepHistory) {
                    if (s.kind == StateKind::DeepHistory)
                        is_deep = true;
                    // Find default target from outgoing transition of history pseudostate
                    for (const auto& t : ir.transitions) {
                        if (t.source == s.name && !t.target.empty()) {
                            default_substate = t.target;
                        }
                    }
                } else {
                    target_substates.push_back(s.name);
                }
            }
        }

        if (default_substate.empty() && !target_substates.empty()) {
            default_substate = target_substates.front();
        }

        // 2. Synthesize shadow history state variable
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
            vdef.type = "string";
            vdef.initial_value = "\"" + default_substate + "\"";
            vdef.description = "Shadow register recording last active substate of composite state " + parent_name;
            ir.variables.push_back(std::move(vdef));
            modified = true;
        }

        // 3. Attach exit action to substates recording active state
        for (auto& s : ir.states) {
            bool is_target = false;
            if (is_deep) {
                // Check if s is descendant of parent
                std::string curr_p = s.parent_state;
                while (!curr_p.empty()) {
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

            if (is_target && s.kind != StateKind::ShallowHistory && s.kind != StateKind::DeepHistory) {
                // Synthesize StoreOp to update shadow register
                StoreOp store;
                store.target = LValueTarget(history_var_name, LValueScope::Register);
                store.op = AssignmentOp::Assign;
                store.expression = "\"" + s.name + "\"";

                ActionSignature exit_sig("__record_history_" + s.name);
                exit_sig.instructions.emplace_back(store, "Record active substate on exit");
                exit_sig.assignments.emplace_back(store.target, store.expression);
                s.exit_actions.push_back(std::move(exit_sig));
                modified = true;
            }
        }

        // 4. Synthesize Choice Restore Pseudostate
        std::string restore_choice_name = parent_name + "_HistoryRestore";
        StateNode restore_node(restore_choice_name, "Choice branch restoring recorded history substate", parent_name);
        restore_node.kind = StateKind::Choice;
        ir.states.push_back(std::move(restore_node));
        ir.choice_nodes.emplace_back(restore_choice_name);

        // Add restore branch transitions from choice to each substate
        for (const auto& sub_name : target_substates) {
            std::string grd = history_var_name + " == \"" + sub_name + "\"";
            TransitionEdge branch(restore_choice_name, sub_name, "", grd, std::nullopt, "Restore branch to " + sub_name,
                                  TransitionEdgeKind::Internal, 0);
            ir.transitions.push_back(std::move(branch));
        }

        // Add default/else fallback branch
        TransitionEdge fallback(restore_choice_name, default_substate, "", "else", std::nullopt,
                                "Default history restore fallback", TransitionEdgeKind::Internal, 0);
        ir.transitions.push_back(std::move(fallback));

        // 5. Retarget incoming transitions pointing to history to the restore choice node
        for (auto& t : ir.transitions) {
            bool targets_history = t.target_is_history || t.target_is_deep_history;
            if (!targets_history) {
                const auto* dst = ir.find_state(t.target);
                if (dst != nullptr && (dst->kind == StateKind::ShallowHistory || dst->kind == StateKind::DeepHistory) &&
                    dst->parent_state == parent_name) {
                    targets_history = true;
                }
            }

            if (targets_history && (t.target == parent_name || t.target.find(parent_name) != std::string::npos)) {
                t.target = restore_choice_name;
                t.target_id = compute_deterministic_id(restore_choice_name);
                t.target_ids = {restore_choice_name};
                t.multi_target_ids = {t.target_id};
                t.target_is_history = false;
                t.target_is_deep_history = false;
                modified = true;
            }
        }
    }

    // 6. Prune obsolete History state nodes
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

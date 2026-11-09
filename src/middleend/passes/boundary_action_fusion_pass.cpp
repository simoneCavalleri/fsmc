#include "fsm/middleend/passes/boundary_action_fusion_pass.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "fsm/ir/action.hpp"

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

namespace {

std::vector<std::string> get_ancestor_chain(const FsmIr& ir, const std::string& state_name) {
    std::vector<std::string> chain;
    std::unordered_set<std::string> visited;
    std::string curr = state_name;
    while (!curr.empty() && visited.insert(curr).second) {
        chain.push_back(curr);
        const auto* st = ir.find_state(curr);
        curr = (st != nullptr) ? st->parent_state : "";
    }
    return chain;
}

std::string compute_lca(const FsmIr& ir, const std::string& src, const std::string& dst) {
    if (src == dst) {
        const auto* s = ir.find_state(src);
        return (s != nullptr) ? s->parent_state : "";
    }

    auto src_chain = get_ancestor_chain(ir, src);
    auto dst_chain = get_ancestor_chain(ir, dst);

    std::unordered_set<std::string> dst_set(dst_chain.begin(), dst_chain.end());
    for (const auto& s : src_chain) {
        if (dst_set.count(s) != 0) {
            return s;
        }
    }
    return "";
}

}  // namespace

bool BoundaryActionFusionPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    bool modified = false;
    std::unordered_set<std::string> states_cleared_exit;
    std::unordered_set<std::string> states_cleared_entry;
    std::unordered_set<std::string> states_unfused_exit;
    std::unordered_set<std::string> states_unfused_entry;

    auto append_action_to_fused = [](ActionSignature& fused_act, const ActionSignature& act) {
        const bool has_structured_ops = !act.instructions.empty() || !act.assignments.empty();
        for (const auto& inst : act.instructions) {
            fused_act.instructions.push_back(inst);
        }
        for (const auto& asgn : act.assignments) {
            fused_act.assignments.push_back(asgn);
        }
        if (!has_structured_ops && (!act.invocation.empty() || !act.name.empty())) {
            ActionCallOp call;
            call.function_name = !act.invocation.empty() ? act.invocation : act.name;
            fused_act.instructions.emplace_back(std::move(call), "Fused callback " + act.name);
        }
    };

    for (auto& t : ir.transitions) {
        if (t.kind == TransitionEdgeKind::Internal) {
            continue;
        }

        const auto* src_st = t.source.empty() ? nullptr : ir.find_state(t.source);
        const auto* dst_st = t.target.empty() ? nullptr : ir.find_state(t.target);
        if (src_st == nullptr || dst_st == nullptr) {
            if (src_st != nullptr) {
                for (const auto& anc : get_ancestor_chain(ir, src_st->name)) {
                    states_unfused_exit.insert(anc);
                }
            }
            if (dst_st != nullptr) {
                for (const auto& anc : get_ancestor_chain(ir, dst_st->name)) {
                    states_unfused_entry.insert(anc);
                }
            }
            continue;
        }

        std::string lca = compute_lca(ir, t.source, t.target);

        // 1. Ascend from source up to LCA (excluding LCA); collect names for post-fusion clearing.
        std::vector<const StateNode*> exit_path;
        std::vector<std::string> exit_path_names;
        std::unordered_set<std::string> visited_exit;
        std::string curr = t.source;
        while (!curr.empty() && curr != lca && visited_exit.insert(curr).second) {
            const auto* s = ir.find_state(curr);
            if (s != nullptr) {
                exit_path.push_back(s);
                exit_path_names.push_back(s->name);
                curr = s->parent_state;
            } else {
                break;
            }
        }

        // 2. Descend from below LCA down to target; collect names for post-fusion clearing.
        std::vector<const StateNode*> entry_path_rev;
        std::vector<std::string> entry_path_names_rev;
        std::unordered_set<std::string> visited_entry;
        curr = t.target;
        while (!curr.empty() && curr != lca && visited_entry.insert(curr).second) {
            const auto* s = ir.find_state(curr);
            if (s != nullptr) {
                entry_path_rev.push_back(s);
                entry_path_names_rev.push_back(s->name);
                curr = s->parent_state;
            } else {
                break;
            }
        }
        std::vector<const StateNode*> entry_path(entry_path_rev.rbegin(), entry_path_rev.rend());
        std::vector<std::string> entry_path_names(entry_path_names_rev.rbegin(), entry_path_names_rev.rend());

        // Check if any exit or entry actions exist along the path
        bool has_boundary_actions = false;
        for (const auto* s : exit_path) {
            if (!s->exit_actions.empty()) {
                has_boundary_actions = true;
                break;
            }
        }
        if (!has_boundary_actions) {
            for (const auto* s : entry_path) {
                if (!s->entry_actions.empty()) {
                    has_boundary_actions = true;
                    break;
                }
            }
        }

        if (!has_boundary_actions) {
            continue;
        }

        // 3. Synthesize atomic fused action
        ActionSignature fused_act(t.transition_action.has_value() ? t.transition_action->name
                                                                  : "fused_" + t.source + "_to_" + t.target);

        // Append exit actions (in ascending leaf-to-LCA order)
        for (const auto* s : exit_path) {
            for (const auto& act : s->exit_actions) {
                append_action_to_fused(fused_act, act);
            }
        }

        // Append transition action
        if (t.transition_action.has_value()) {
            append_action_to_fused(fused_act, *t.transition_action);
        }

        // Append entry actions (in descending LCA-to-target order)
        for (const auto* s : entry_path) {
            for (const auto& act : s->entry_actions) {
                append_action_to_fused(fused_act, act);
            }
        }

        t.transition_action = std::move(fused_act);

        for (const auto& name : exit_path_names) {
            states_cleared_exit.insert(name);
        }
        for (const auto& name : entry_path_names) {
            states_cleared_entry.insert(name);
        }

        modified = true;
    }

    // 4. Clear lowered hooks to prevent double execution in the generated C++ runtime.
    //    After fusion across all transitions, StateNode lifecycle hooks (on_exit / on_enter)
    //    are cleared so they are not emitted again by the code generator, except on initial
    //    states or states with unfused transitions.
    for (const auto& name : states_cleared_exit) {
        if (states_unfused_exit.count(name) != 0) {
            continue;
        }
        if (auto* mutable_s = ir.find_state_mut(name)) {
            mutable_s->exit_actions.clear();
        }
    }
    for (const auto& name : states_cleared_entry) {
        if (states_unfused_entry.count(name) != 0) {
            continue;
        }
        if (auto* mutable_s = ir.find_state_mut(name)) {
            const bool is_machine_initial =
                (!ir.initial_state.empty() && (mutable_s->name == ir.initial_state || mutable_s->id == ir.initial_state)) ||
                (!ir.initial_state_id.empty() &&
                 (mutable_s->name == ir.initial_state_id || mutable_s->id == ir.initial_state_id));
            bool is_composite_initial = false;
            for (const auto& other_s : ir.states) {
                if (!other_s.initial_sub_state.empty() &&
                    (other_s.initial_sub_state == mutable_s->name || other_s.initial_sub_state == mutable_s->id)) {
                    is_composite_initial = true;
                    break;
                }
            }
            if (!is_machine_initial && !is_composite_initial) {
                mutable_s->entry_actions.clear();
            }
        }
    }

    if (modified) {
        diag.report(Diagnostic::info(
            "I_BOUNDARY_FUSED", "Successfully fused hierarchical LCA boundary action cascades into transition edges."));
    }
    return modified;
}

}  // namespace fsm::middleend::passes

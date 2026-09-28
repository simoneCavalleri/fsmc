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
    std::string curr = state_name;
    while (!curr.empty()) {
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

    for (auto& t : ir.transitions) {
        if (t.source.empty() || t.target.empty() || t.kind == TransitionEdgeKind::Internal) {
            continue;
        }

        const auto* src_st = ir.find_state(t.source);
        const auto* dst_st = ir.find_state(t.target);
        if (src_st == nullptr || dst_st == nullptr) {
            continue;
        }

        std::string lca = compute_lca(ir, t.source, t.target);

        // 1. Ascend from source up to LCA (excluding LCA); collect names for post-fusion clearing.
        std::vector<const StateNode*> exit_path;
        std::vector<std::string> exit_path_names;
        std::string curr = t.source;
        while (!curr.empty() && curr != lca) {
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
        curr = t.target;
        while (!curr.empty() && curr != lca) {
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
                for (const auto& inst : act.instructions) {
                    fused_act.instructions.push_back(inst);
                }
                for (const auto& asgn : act.assignments) {
                    fused_act.assignments.push_back(asgn);
                }
            }
        }

        // Append transition action
        if (t.transition_action.has_value()) {
            for (const auto& inst : t.transition_action->instructions) {
                fused_act.instructions.push_back(inst);
            }
            for (const auto& asgn : t.transition_action->assignments) {
                fused_act.assignments.push_back(asgn);
            }
        }

        // Append entry actions (in descending LCA-to-target order)
        for (const auto* s : entry_path) {
            for (const auto& act : s->entry_actions) {
                for (const auto& inst : act.instructions) {
                    fused_act.instructions.push_back(inst);
                }
                for (const auto& asgn : act.assignments) {
                    fused_act.assignments.push_back(asgn);
                }
            }
        }

        t.transition_action = std::move(fused_act);

        // 4. Clear lowered hooks to prevent double execution in the generated C++ runtime.
        //    After fusion, the StateNode lifecycle hooks (on_exit / on_enter) would be emitted
        //    *again* by the code generator unless cleared here.  This is safe because the pass
        //    is a one-way lowering step: every action that was on the node is now on the edge.
        for (const auto& name : exit_path_names) {
            if (auto* mutable_s = ir.find_state_mut(name)) {
                mutable_s->exit_actions.clear();
            }
        }
        for (const auto& name : entry_path_names) {
            if (auto* mutable_s = ir.find_state_mut(name)) {
                mutable_s->entry_actions.clear();
            }
        }

        modified = true;
    }

    if (modified) {
        diag.report(Diagnostic::info(
            "I_BOUNDARY_FUSED", "Successfully fused hierarchical LCA boundary action cascades into transition edges."));
    }
    return modified;
}

}  // namespace fsm::middleend::passes

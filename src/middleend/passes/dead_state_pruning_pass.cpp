#include "fsm/middleend/passes/dead_state_pruning_pass.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

bool DeadStatePruningPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    // 1. Resolve initial state (prefer name; fallback to lookup via initial_state_id)
    std::string root = ir.initial_state;
    if (root.empty()) {
        if (const auto* s = ir.find_state_by_id(ir.initial_state_id)) {
            root = s->name;
        } else {
            root = ir.initial_state_id;
        }
    }
    if (root.empty() && !ir.states.empty()) {
        root = ir.states.front().name;
    }
    if (root.empty()) {
        return true;
    }

    // 2. Compute reachable states via BFS (tracking both names and deterministic IDs)
    std::unordered_set<std::string> reachable;
    std::queue<std::string> q;
    q.push(root);
    reachable.insert(root);
    if (const auto* root_node = ir.find_state_by_name(root)) {
        if (!root_node->id.empty()) {
            reachable.insert(root_node->id);
        }
    } else if (const auto* root_by_id = ir.find_state_by_id(root)) {
        reachable.insert(root_by_id->name);
        reachable.insert(root_by_id->id);
    }

    while (!q.empty()) {
        std::string curr = q.front();
        q.pop();

        // Include composite sub-states
        for (const auto& s : ir.states) {
            bool matches_parent = (!s.parent_state.empty() && s.parent_state == curr) ||
                                  (s.parent_id.has_value() && *s.parent_id == curr);
            if (matches_parent && reachable.count(s.name) == 0) {
                reachable.insert(s.name);
                if (!s.id.empty()) {
                    reachable.insert(s.id);
                }
                q.push(s.name);
            }
        }

        // Include parent/ancestor states and initial sub-state
        const auto* curr_node = ir.find_state_by_name(curr);
        if (!curr_node) {
            curr_node = ir.find_state_by_id(curr);
        }
        if (curr_node) {
            if (!curr_node->parent_state.empty() && reachable.count(curr_node->parent_state) == 0) {
                reachable.insert(curr_node->parent_state);
                if (curr_node->parent_id.has_value() && !curr_node->parent_id->empty()) {
                    reachable.insert(*curr_node->parent_id);
                }
                q.push(curr_node->parent_state);
            }
            if (!curr_node->initial_sub_state.empty() && reachable.count(curr_node->initial_sub_state) == 0) {
                reachable.insert(curr_node->initial_sub_state);
                if (const auto* sub_node = ir.find_state_by_name(curr_node->initial_sub_state)) {
                    if (!sub_node->id.empty()) {
                        reachable.insert(sub_node->id);
                    }
                }
                q.push(curr_node->initial_sub_state);
            }
        }

        for (const auto& t : ir.transitions) {
            bool matches_source = (t.source == curr || (!t.source_id.empty() && t.source_id == curr));
            if (matches_source) {
                // Statically dead transition
                if (t.guard.has_value() && *t.guard == "false") {
                    continue;
                }
                std::string target_to_push;
                if (!t.target.empty() && reachable.count(t.target) == 0) {
                    reachable.insert(t.target);
                    if (!t.target_id.empty()) {
                        reachable.insert(t.target_id);
                    }
                    if (const auto* tn = ir.find_state_by_name(t.target)) {
                        if (!tn->id.empty()) {
                            reachable.insert(tn->id);
                        }
                    }
                    target_to_push = t.target;
                } else if (!t.target_id.empty() && reachable.count(t.target_id) == 0) {
                    reachable.insert(t.target_id);
                    if (const auto* tn = ir.find_state_by_id(t.target_id)) {
                        reachable.insert(tn->name);
                        target_to_push = tn->name;
                    } else {
                        target_to_push = t.target_id;
                    }
                }
                if (!target_to_push.empty()) {
                    q.push(target_to_push);
                }
            }
        }
    }

    if (!prune_) {
        return true;
    }

    // 3. Prune unreachable states (including unreachable Choice pseudostates)
    std::size_t initial_state_count = ir.states.size();
    ir.states.erase(std::remove_if(ir.states.begin(), ir.states.end(),
                                   [&](const StateNode& s) {
                                       return reachable.count(s.name) == 0 && reachable.count(s.id) == 0;
                                   }),
                    ir.states.end());

    // Prune unreachable choice nodes from ir.choice_nodes
    ir.choice_nodes.erase(std::remove_if(ir.choice_nodes.begin(), ir.choice_nodes.end(),
                                        [&](const ChoiceNodeModel& c) {
                                            return reachable.count(c.name) == 0;
                                        }),
                          ir.choice_nodes.end());

    std::size_t pruned_states = initial_state_count - ir.states.size();

    // 4. Prune dead transitions (from unreachable states or with guard == "false")
    std::size_t initial_trans_count = ir.transitions.size();
    ir.transitions.erase(std::remove_if(ir.transitions.begin(), ir.transitions.end(),
                                        [&](const TransitionEdge& t) {
                                            if (t.guard.has_value() && *t.guard == "false")
                                                return true;
                                            bool src_reachable = reachable.count(t.source) > 0 ||
                                                                 (!t.source_id.empty() && reachable.count(t.source_id) > 0);
                                            return !src_reachable && (!t.source.empty() || !t.source_id.empty());
                                        }),
                         ir.transitions.end());

    std::size_t pruned_trans = initial_trans_count - ir.transitions.size();

    if (pruned_states > 0 || pruned_trans > 0) {
        diag.report(Diagnostic::info("I_PRUNED_DEAD_CODE", "DeadStatePruningPass pruned " +
                                                               std::to_string(pruned_states) +
                                                               " unreachable states and " +
                                                               std::to_string(pruned_trans) + " dead transitions."));
    }

    return true;
}

}  // namespace fsm::middleend::passes

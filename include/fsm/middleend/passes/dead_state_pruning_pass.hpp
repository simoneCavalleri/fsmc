#pragma once

#include <algorithm>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

/**
 * @brief Target-Agnostic Middle-End Pass: Dead State Elimination & Dead Transition Pruning.
 *
 * Traverses reachable state space from root initial state and removes unreachable states
 * and statically dead transitions (e.g., transitions with guard == "false") to reduce code bloat.
 */
class DeadStatePruningPass {
  public:
    explicit DeadStatePruningPass(bool enable_pruning = true) : prune_(enable_pruning) {}

    [[nodiscard]] static std::string name() { return "DeadStatePruning"; }
    [[nodiscard]] static std::string description() {
        return "Removes unreachable states and statically dead transitions from the IR graph";
    }

    void set_prune(bool enable) { prune_ = enable; }
    [[nodiscard]] bool is_prune_enabled() const noexcept { return prune_; }

    bool run(FsmIr& ir, DiagnosticEngine& diag) {
        std::string root = ir.initial_state_id.empty() ? ir.initial_state : ir.initial_state_id;
        if (root.empty() && !ir.states.empty()) {
            root = ir.states.front().name;
        }
        if (root.empty()) {
            return true;
        }

        // 1. Compute reachable states via BFS
        std::unordered_set<std::string> reachable;
        std::queue<std::string> q;
        q.push(root);
        reachable.insert(root);

        while (!q.empty()) {
            std::string curr = q.front();
            q.pop();

            // Include composite sub-states
            for (const auto& s : ir.states) {
                if (s.parent_state == curr && reachable.count(s.name) == 0) {
                    reachable.insert(s.name);
                    q.push(s.name);
                }
            }

            for (const auto& t : ir.transitions) {
                if (t.source == curr) {
                    // Statically dead transition
                    if (t.guard.has_value() && *t.guard == "false") {
                        continue;
                    }
                    if (!t.target.empty() && reachable.count(t.target) == 0) {
                        reachable.insert(t.target);
                        q.push(t.target);
                    }
                }
            }
        }

        if (!prune_) {
            return true;
        }

        // 2. Prune unreachable states
        std::size_t initial_state_count = ir.states.size();
        ir.states.erase(std::remove_if(ir.states.begin(), ir.states.end(),
                                       [&](const StateNode& s) {
                                           if (s.kind == StateKind::Final || ir.is_choice_node(s.name))
                                               return false;
                                           return reachable.count(s.name) == 0;
                                       }),
                        ir.states.end());

        std::size_t pruned_states = initial_state_count - ir.states.size();

        // 3. Prune dead transitions (from unreachable states or with guard == "false")
        std::size_t initial_trans_count = ir.transitions.size();
        ir.transitions.erase(std::remove_if(ir.transitions.begin(), ir.transitions.end(),
                                            [&](const TransitionEdge& t) {
                                                if (t.guard.has_value() && *t.guard == "false")
                                                    return true;
                                                return reachable.count(t.source) == 0 && !t.source.empty();
                                            }),
                             ir.transitions.end());

        std::size_t pruned_trans = initial_trans_count - ir.transitions.size();

        if (pruned_states > 0 || pruned_trans > 0) {
            diag.report(
                Diagnostic::info("I_PRUNED_DEAD_CODE", "DeadStatePruningPass pruned " + std::to_string(pruned_states) +
                                                           " unreachable states and " + std::to_string(pruned_trans) +
                                                           " dead transitions."));
        }

        return true;
    }

  private:
    bool prune_{true};
};

}  // namespace fsm::middleend::passes

namespace fsm::middleend {
using passes::DeadStatePruningPass;
}  // namespace fsm::middleend

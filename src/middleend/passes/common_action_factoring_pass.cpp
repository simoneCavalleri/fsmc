/**
 * @file common_action_factoring_pass.cpp
 * @brief Implementation of CommonActionFactoringPass.
 */

#include "fsm/middleend/passes/common_action_factoring_pass.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace fsm::middleend::passes {

using namespace ir;

bool CommonActionFactoringPass::run(FsmIr& ir, DiagnosticEngine& /*diag*/) {
    bool modified = false;

    auto matches_state = [](const std::string& edge_ref_name, const std::string& edge_ref_id, const StateNode& st) {
        if (!edge_ref_name.empty() && (edge_ref_name == st.name || edge_ref_name == st.id)) {
            return true;
        }
        if (!edge_ref_id.empty() && (edge_ref_id == st.id || edge_ref_id == st.name)) {
            return true;
        }
        return false;
    };

    // 1. Convergent Transition Factoring:
    // When multiple transitions enter the same target state and all share the identical
    // transition action, factor the action into the target state's entry actions.
    for (auto& state : ir.states) {
        // Semantics safeguard: Do not factor into initial state entry actions to prevent
        // spurious execution during power-on initialization without transition firing.
        bool is_initial =
            (!ir.initial_state.empty() && (state.name == ir.initial_state || state.id == ir.initial_state)) ||
            (!ir.initial_state_id.empty() && (state.name == ir.initial_state_id || state.id == ir.initial_state_id));
        if (is_initial) {
            continue;
        }

        std::vector<std::size_t> incoming_indices;
        for (std::size_t i = 0; i < ir.transitions.size(); ++i) {
            const auto& t = ir.transitions[i];
            if (t.kind == ir::TransitionEdgeKind::Internal) {
                continue;
            }
            if (matches_state(t.target, t.target_id, state)) {
                incoming_indices.push_back(i);
            }
        }

        if (incoming_indices.size() >= 2) {
            bool all_have_action = true;
            for (std::size_t idx : incoming_indices) {
                const auto& t = ir.transitions[idx];
                if (!t.transition_action.has_value() || t.transition_action->empty()) {
                    all_have_action = false;
                    break;
                }
            }

            if (all_have_action) {
                const auto& first_act = *ir.transitions[incoming_indices.front()].transition_action;
                bool all_identical = true;
                for (std::size_t i = 1; i < incoming_indices.size(); ++i) {
                    if (!(*ir.transitions[incoming_indices[i]].transition_action == first_act)) {
                        all_identical = false;
                        break;
                    }
                }

                if (all_identical) {
                    // Prepend factored action into target state entry actions
                    state.entry_actions.insert(state.entry_actions.begin(), first_act);
                    for (std::size_t idx : incoming_indices) {
                        ir.transitions[idx].transition_action = std::nullopt;
                    }
                    modified = true;
                }
            }
        }
    }

    // 2. Divergent Transition Factoring:
    // When all outgoing transitions from a source state share the identical transition action,
    // factor the action into the source state's exit actions.
    for (auto& state : ir.states) {
        std::vector<std::size_t> outgoing_indices;
        for (std::size_t i = 0; i < ir.transitions.size(); ++i) {
            const auto& t = ir.transitions[i];
            if (t.kind == ir::TransitionEdgeKind::Internal) {
                continue;
            }
            if (matches_state(t.source, t.source_id, state)) {
                outgoing_indices.push_back(i);
            }
        }

        if (outgoing_indices.size() >= 2) {
            bool all_have_action = true;
            for (std::size_t idx : outgoing_indices) {
                const auto& t = ir.transitions[idx];
                if (!t.transition_action.has_value() || t.transition_action->empty()) {
                    all_have_action = false;
                    break;
                }
            }

            if (all_have_action) {
                const auto& first_act = *ir.transitions[outgoing_indices.front()].transition_action;
                bool all_identical = true;
                for (std::size_t i = 1; i < outgoing_indices.size(); ++i) {
                    if (!(*ir.transitions[outgoing_indices[i]].transition_action == first_act)) {
                        all_identical = false;
                        break;
                    }
                }

                if (all_identical) {
                    // Append factored action into source state exit actions
                    state.exit_actions.push_back(first_act);
                    for (std::size_t idx : outgoing_indices) {
                        ir.transitions[idx].transition_action = std::nullopt;
                    }
                    modified = true;
                }
            }
        }
    }
    (void)modified;
    return true;
}

}  // namespace fsm::middleend::passes

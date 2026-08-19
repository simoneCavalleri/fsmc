#pragma once

#include <queue>
#include <set>
#include <string>
#include <vector>

#include "fsm_model.hpp"

namespace fsm::codegen {

struct ValidationResult {
    bool is_valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

class FsmValidator {
  public:
    static ValidationResult validate(const FsmModel& model) {
        ValidationResult result;

        if (model.states.empty() && model.choice_nodes.empty()) {
            result.is_valid = false;
            result.errors.emplace_back("State machine does not contain any states.");
            return result;
        }

        std::set<std::string> node_names;
        for (const auto& state_item : model.states) {
            node_names.insert(state_item.name);
        }
        for (const auto& choice_item : model.choice_nodes) {
            node_names.insert(choice_item.name);
        }

        // Check initial state
        if (model.initial_state.empty()) {
            if (!model.states.empty()) {
                result.warnings.emplace_back("No initial state '[*]' specified. Defaulting to first state: '" +
                                             model.states[0].name + "'.");
            }
        } else if (!node_names.contains(model.initial_state)) {
            result.is_valid = false;
            result.errors.emplace_back("Initial state '" + model.initial_state + "' is not defined in the state list.");
        }

        // Check transitions validity
        for (const auto& transition_item : model.transitions) {
            if (!node_names.contains(transition_item.source)) {
                result.is_valid = false;
                result.errors.emplace_back("Unknown source state/node in transition: '" + transition_item.source +
                                           "'.");
            }
            if (!node_names.contains(transition_item.target)) {
                result.is_valid = false;
                result.errors.emplace_back("Unknown target state/node in transition: '" + transition_item.target +
                                           "'.");
            }
            if (transition_item.event.empty() && !model.is_choice_node(transition_item.source)) {
                result.warnings.emplace_back("Transition from '" + transition_item.source + "' to '" +
                                             transition_item.target + "' has no event specified.");
            }
        }

        // Check choice nodes have outgoing transitions
        for (const auto& choice_item : model.choice_nodes) {
            bool has_outgoing = false;
            for (const auto& transition_item : model.transitions) {
                if (transition_item.source == choice_item.name) {
                    has_outgoing = true;
                    break;
                }
            }
            if (!has_outgoing) {
                result.errors.emplace_back("Choice pseudostate '" + choice_item.name + "' has no outgoing branches.");
                result.is_valid = false;
            }
        }

        // Check reachability from initial state
        if (result.is_valid && !model.initial_state.empty()) {
            std::set<std::string> reachable;
            std::queue<std::string> bfs_queue;
            bfs_queue.push(model.initial_state);
            reachable.insert(model.initial_state);

            while (!bfs_queue.empty()) {
                const std::string current = bfs_queue.front();
                bfs_queue.pop();

                for (const auto& transition_item : model.transitions) {
                    if (transition_item.source == current && !reachable.contains(transition_item.target)) {
                        reachable.insert(transition_item.target);
                        bfs_queue.push(transition_item.target);
                    }
                }
            }

            for (const auto& state_item : model.states) {
                if (!reachable.contains(state_item.name)) {
                    result.warnings.emplace_back("State unreachable from initial state: '" + state_item.name + "'.");
                }
            }
        }

        return result;
    }
};

}  // namespace fsm::codegen

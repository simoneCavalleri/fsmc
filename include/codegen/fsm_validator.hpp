#pragma once

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "fsm_model.hpp"

namespace fsm::codegen {

enum class DiagnosticSeverity : std::uint8_t {
    Info,
    Warning,
    Error,
    SafetyCritical
};

struct DiagnosticMessage {
    DiagnosticSeverity severity;
    std::string category;
    std::string message;
};

struct ValidationResult {
    bool is_valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<DiagnosticMessage> diagnostics;

    void add_error(const std::string& category, const std::string& msg) {
        is_valid = false;
        errors.push_back(msg);
        diagnostics.push_back({DiagnosticSeverity::Error, category, msg});
    }

    void add_warning(const std::string& category, const std::string& msg) {
        warnings.push_back(msg);
        diagnostics.push_back({DiagnosticSeverity::Warning, category, msg});
    }

    void add_safety_critical(const std::string& category, const std::string& msg) {
        warnings.push_back("[SAFETY CRITICAL] " + msg);
        diagnostics.push_back({DiagnosticSeverity::SafetyCritical, category, msg});
    }

    void add_info(const std::string& category, const std::string& msg) {
        diagnostics.push_back({DiagnosticSeverity::Info, category, msg});
    }
};

class FsmValidator {
  public:
    static ValidationResult validate(const FsmModel& model) {
        ValidationResult result;

        if (model.states.empty() && model.choice_nodes.empty()) {
            result.add_error("Structure", "State machine does not contain any states.");
            return result;
        }

        std::set<std::string> node_names;
        for (const auto& state_item : model.states) {
            node_names.insert(state_item.name);
        }
        for (const auto& choice_item : model.choice_nodes) {
            node_names.insert(choice_item.name);
        }

        // 1. Check Initial State
        validate_initial_state(model, node_names, result);

        // 2. Check Transitions Validity & Unknown Nodes
        validate_transition_endpoints(model, node_names, result);

        // 3. Check Choice Pseudostates Completeness & Conflict
        validate_choice_pseudostates(model, result);

        // 4. Check Reachability from Initial State
        if (result.is_valid && !model.initial_state.empty()) {
            validate_reachability(model, result);
        }

        // 5. Formal Verification: Livelock (Eventless Transition Cycles)
        validate_livelock_cycles(model, result);

        // 6. Formal Verification: Trap / Deadlock States
        validate_deadlock_states(model, result);

        // 7. Formal Verification: Non-Deterministic Transition Conflicts
        validate_transition_determinism(model, result);

        // 8. Formal Verification: Timed Transition Priority & Redundancy
        validate_timed_transitions(model, result);

        return result;
    }

  private:
    static void validate_initial_state(const FsmModel& model, const std::set<std::string>& node_names,
                                       ValidationResult& result) {
        if (model.initial_state.empty()) {
            if (!model.states.empty()) {
                result.add_warning("InitialState",
                                   "No initial state '[*]' specified. Defaulting to first state: '" +
                                       model.states[0].name + "'.");
            }
        } else if (node_names.count(model.initial_state) == 0) {
            result.add_error("InitialState",
                             "Initial state '" + model.initial_state + "' is not defined in the state list.");
        }
    }

    static void validate_transition_endpoints(const FsmModel& model, const std::set<std::string>& node_names,
                                              ValidationResult& result) {
        for (const auto& transition_item : model.transitions) {
            if (node_names.count(transition_item.source) == 0) {
                result.add_error("Transition",
                                 "Unknown source state/node in transition: '" + transition_item.source + "'.");
            }
            if (node_names.count(transition_item.target) == 0) {
                result.add_error("Transition",
                                 "Unknown target state/node in transition: '" + transition_item.target + "'.");
            }
            if (transition_item.event.empty() && !model.is_choice_node(transition_item.source)) {
                result.add_info("Transition", "Transition from '" + transition_item.source + "' to '" +
                                                  transition_item.target + "' is eventless (immediate).");
            }
        }
    }

    static void validate_choice_pseudostates(const FsmModel& model, ValidationResult& result) {
        for (const auto& choice_item : model.choice_nodes) {
            std::vector<const TransitionModel*> outgoing;
            for (const auto& transition_item : model.transitions) {
                if (transition_item.source == choice_item.name) {
                    outgoing.push_back(&transition_item);
                }
            }

            if (outgoing.empty()) {
                result.add_error("Choice",
                                 "Choice pseudostate '" + choice_item.name + "' has no outgoing branches.");
                continue;
            }

            bool has_else_branch = false;
            std::set<std::string> seen_guards;
            for (const auto* out_trans : outgoing) {
                if (!out_trans->guard.has_value() || out_trans->guard->empty()) {
                    has_else_branch = true;
                } else {
                    if (seen_guards.count(*out_trans->guard) != 0) {
                        result.add_warning("Choice", "Choice pseudostate '" + choice_item.name +
                                                         "' has duplicate/conflicting guard condition '" +
                                                         *out_trans->guard + "' on multiple outgoing branches.");
                    }
                    seen_guards.insert(*out_trans->guard);
                }
            }

            if (!has_else_branch && outgoing.size() > 1) {
                result.add_safety_critical(
                    "Choice", "Choice pseudostate '" + choice_item.name +
                                  "' lacks an unconditional else/default fallback branch (potential stall).");
            }
        }
    }

    static void validate_reachability(const FsmModel& model, ValidationResult& result) {
        std::set<std::string> reachable;
        std::queue<std::string> bfs_queue;
        bfs_queue.push(model.initial_state);
        reachable.insert(model.initial_state);

        while (!bfs_queue.empty()) {
            const std::string current = bfs_queue.front();
            bfs_queue.pop();

            for (const auto& transition_item : model.transitions) {
                if (transition_item.source == current && reachable.count(transition_item.target) == 0) {
                    reachable.insert(transition_item.target);
                    bfs_queue.push(transition_item.target);
                }
            }
        }

        for (const auto& state_item : model.states) {
            if (reachable.count(state_item.name) == 0) {
                result.add_warning("Reachability",
                                   "State unreachable from initial state: '" + state_item.name + "'.");
            }
        }
    }

    static void validate_livelock_cycles(const FsmModel& model, ValidationResult& result) {
        // Build eventless adjacency list
        std::map<std::string, std::vector<std::string>> eventless_adj;
        for (const auto& transition_item : model.transitions) {
            if (transition_item.event.empty() || transition_item.event == "AnonymousEvent" ||
                transition_item.event == "anonymous") {
                eventless_adj[transition_item.source].push_back(transition_item.target);
            }
        }

        // 0 = unvisited, 1 = visiting (in stack), 2 = visited
        std::map<std::string, int> visit_state;
        std::vector<std::string> path;

        std::function<bool(const std::string&)> dfs_cycle = [&](const std::string& node) -> bool {
            visit_state[node] = 1;
            path.push_back(node);

            for (const auto& next_node : eventless_adj[node]) {
                if (visit_state[next_node] == 1) {
                    // Cycle detected!
                    std::string cycle_str;
                    bool in_cycle = false;
                    for (const auto& p_item : path) {
                        if (p_item == next_node) {
                            in_cycle = true;
                        }
                        if (in_cycle) {
                            if (!cycle_str.empty()) {
                                cycle_str += " -> ";
                            }
                            cycle_str += p_item;
                        }
                    }
                    cycle_str += " -> " + next_node;
                    result.add_safety_critical("Livelock",
                                               "Infinite cycle of eventless transitions detected: [" + cycle_str + "].");
                    return true;
                }
                if (visit_state[next_node] == 0) {
                    if (dfs_cycle(next_node)) {
                        return true;
                    }
                }
            }

            path.pop_back();
            visit_state[node] = 2;
            return false;
        };

        for (const auto& state_item : model.states) {
            if (visit_state[state_item.name] == 0) {
                dfs_cycle(state_item.name);
            }
        }
    }

    static void validate_deadlock_states(const FsmModel& model, ValidationResult& result) {
        std::map<std::string, int> in_degree;
        std::map<std::string, int> out_degree;

        for (const auto& state_item : model.states) {
            in_degree[state_item.name] = 0;
            out_degree[state_item.name] = 0;
        }

        for (const auto& transition_item : model.transitions) {
            out_degree[transition_item.source]++;
            in_degree[transition_item.target]++;
        }

        for (const auto& state_item : model.states) {
            if (out_degree[state_item.name] == 0 && in_degree[state_item.name] > 0) {
                std::string lower_name = state_item.name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                const bool is_intended_final = (lower_name.find("final") != std::string::npos ||
                                                lower_name.find("end") != std::string::npos ||
                                                lower_name.find("terminate") != std::string::npos ||
                                                lower_name.find("stop") != std::string::npos ||
                                                lower_name == "completed");

                if (!is_intended_final) {
                    result.add_warning("Deadlock", "Potential trap / deadlock state: '" + state_item.name +
                                                       "' has incoming transitions but no outgoing transitions.");
                }
            }
        }
    }

    static void validate_transition_determinism(const FsmModel& model, ValidationResult& result) {
        std::map<std::pair<std::string, std::string>, std::vector<const TransitionModel*>> trans_by_state_event;

        for (const auto& transition_item : model.transitions) {
            if (!transition_item.event.empty()) {
                trans_by_state_event[{transition_item.source, transition_item.event}].push_back(&transition_item);
            }
        }

        for (const auto& pair_item : trans_by_state_event) {
            const auto& src_evt = pair_item.first;
            const auto& trans_list = pair_item.second;

            if (trans_list.size() > 1) {
                int unconditional_count = 0;
                for (const auto* t : trans_list) {
                    if (!t->guard.has_value() || t->guard->empty()) {
                        unconditional_count++;
                    }
                }
                if (unconditional_count > 1) {
                    result.add_safety_critical(
                        "Determinism", "Non-deterministic conflict in state '" + src_evt.first +
                                           "': multiple unconditional transitions on event '" + src_evt.second + "'.");
                }
            }
        }
    }

    static void validate_timed_transitions(const FsmModel& model, ValidationResult& result) {
        std::map<std::pair<std::string, std::string>, int> timer_counts;

        for (const auto& transition_item : model.transitions) {
            if (transition_item.event.find("after_") == 0 || transition_item.event.find("after(") == 0) {
                timer_counts[{transition_item.source, transition_item.event}]++;
            }
        }

        for (const auto& entry : timer_counts) {
            if (entry.second > 1) {
                result.add_warning("TimedTransition", "Duplicate timer transitions defined for duration '" +
                                                          entry.first.second + "' on state '" + entry.first.first + "'.");
            }
        }
    }
};

}  // namespace fsm::codegen

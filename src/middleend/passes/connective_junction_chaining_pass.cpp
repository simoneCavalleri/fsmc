#include "fsm/middleend/passes/connective_junction_chaining_pass.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

namespace {

bool is_else_guard(const std::optional<std::string>& g) {
    if (!g.has_value() || g->empty()) {
        return true;
    }
    const std::string& val = *g;
    return val == "else" || val == "otherwise" || val == "default";
}

std::string combine_two_guards(const std::string& g1, const std::string& g2) {
    if (g1.empty()) return g2;
    if (g2.empty()) return g1;
    return "fsm::and_<" + g1 + ", " + g2 + ">";
}

}  // namespace

bool ConnectiveJunctionChainingPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    // 1. Identify all junction states
    std::unordered_set<std::string> junction_names;
    for (const auto& s : ir.states) {
        if (s.kind == StateKind::Junction) {
            junction_names.insert(s.name);
        }
    }

    if (junction_names.empty()) {
        return false;
    }

    // 2. Build adjacency graph for cycle detection among junctions
    std::unordered_map<std::string, std::vector<std::string>> junc_adj;
    for (const auto& t : ir.transitions) {
        if (junction_names.count(t.source) && junction_names.count(t.target)) {
            junc_adj[t.source].push_back(t.target);
        }
    }

    // 3-color DFS cycle detection: 0 = unvisited, 1 = visiting, 2 = visited
    std::unordered_map<std::string, int> visit_state;
    for (const auto& j : junction_names) {
        visit_state[j] = 0;
    }

    bool has_cycle = false;
    std::function<void(const std::string&)> check_cycle = [&](const std::string& u) {
        visit_state[u] = 1;
        if (auto it = junc_adj.find(u); it != junc_adj.end()) {
            for (const auto& v : it->second) {
                if (visit_state[v] == 1) {
                    has_cycle = true;
                    diag.report(Diagnostic::error(
                        "EJUNC001", "Cyclic connective junction path detected involving '" + u + "' -> '" + v + "'"));
                    return;
                }
                if (visit_state[v] == 0) {
                    check_cycle(v);
                    if (has_cycle) return;
                }
            }
        }
        visit_state[u] = 2;
    };

    for (const auto& j : junction_names) {
        if (visit_state[j] == 0) {
            check_cycle(j);
            if (has_cycle) {
                return false;
            }
        }
    }

    // 3. Build outgoing transitions map for fast DFS traversal
    std::unordered_map<std::string, std::vector<TransitionEdge>> outgoing_map;
    for (const auto& t : ir.transitions) {
        outgoing_map[t.source].push_back(t);
    }

    // 4. Trace complete paths: non-junction source -> junction* -> non-junction target
    std::vector<TransitionEdge> synthesized_transitions;

    // DFS helper
    std::function<void(std::vector<TransitionEdge>&)> explore_paths =
        [&](std::vector<TransitionEdge>& current_path) {
            const auto& last_edge = current_path.back();
            const std::string& current_dest = last_edge.target;

            if (junction_names.count(current_dest) == 0) {
                // Reached a non-junction destination state: synthesize compound transition!
                const auto& first_edge = current_path.front();

                TransitionEdge composite;
                composite.source = first_edge.source;
                composite.source_id = first_edge.source_id;
                composite.source_ids = first_edge.source_ids;
                composite.target = last_edge.target;
                composite.target_id = last_edge.target_id;
                composite.target_ids = last_edge.target_ids;
                composite.target_is_history = last_edge.target_is_history;
                composite.target_is_deep_history = last_edge.target_is_deep_history;
                composite.kind = TransitionEdgeKind::External;
                composite.priority = (last_edge.priority > 0) ? last_edge.priority : first_edge.priority;

                // Event & Trigger: inherit from the first segment that defines them
                for (const auto& edge : current_path) {
                    if (!edge.event.empty()) {
                        composite.event = edge.event;
                        composite.trigger = edge.trigger;
                        break;
                    }
                }

                // Combine Guards
                std::string combined_guard;
                for (const auto& edge : current_path) {
                    if (!is_else_guard(edge.guard)) {
                        combined_guard = combine_two_guards(combined_guard, *edge.guard);
                    }
                }
                if (!combined_guard.empty()) {
                    composite.guard = combined_guard;
                    composite.guard_ast = GuardAstNode(combined_guard);
                }

                // Combine Condition Actions
                std::vector<std::string> cond_action_names;
                std::vector<ActionAssignment> cond_assignments;
                for (const auto& edge : current_path) {
                    if (edge.condition_action.has_value()) {
                        if (!edge.condition_action->name.empty()) {
                            cond_action_names.push_back(edge.condition_action->name);
                        }
                        for (const auto& assign : edge.condition_action->assignments) {
                            cond_assignments.push_back(assign);
                        }
                    }
                }
                if (!cond_action_names.empty() || !cond_assignments.empty()) {
                    std::string combined_cond_name;
                    for (size_t i = 0; i < cond_action_names.size(); ++i) {
                        if (i > 0) combined_cond_name += "_";
                        combined_cond_name += cond_action_names[i];
                    }
                    if (combined_cond_name.empty()) {
                        combined_cond_name = "ChainedCondAction";
                    }
                    ActionSignature sig(combined_cond_name);
                    sig.assignments = std::move(cond_assignments);
                    composite.condition_action = std::move(sig);
                }

                // Combine Transition Actions
                std::vector<std::string> trans_action_names;
                std::vector<ActionAssignment> trans_assignments;
                for (const auto& edge : current_path) {
                    if (edge.transition_action.has_value()) {
                        if (!edge.transition_action->name.empty()) {
                            trans_action_names.push_back(edge.transition_action->name);
                        }
                        for (const auto& assign : edge.transition_action->assignments) {
                            trans_assignments.push_back(assign);
                        }
                    }
                }
                if (!trans_action_names.empty() || !trans_assignments.empty()) {
                    std::string combined_trans_name;
                    for (size_t i = 0; i < trans_action_names.size(); ++i) {
                        if (i > 0) combined_trans_name += "_";
                        combined_trans_name += trans_action_names[i];
                    }
                    if (combined_trans_name.empty()) {
                        combined_trans_name = "ChainedTransAction";
                    }
                    ActionSignature sig(combined_trans_name);
                    sig.assignments = std::move(trans_assignments);
                    composite.transition_action = std::move(sig);
                }

                // Combine clock resets
                std::unordered_set<std::string> seen_resets;
                for (const auto& edge : current_path) {
                    for (const auto& cr : edge.clock_resets) {
                        if (seen_resets.insert(cr).second) {
                            composite.clock_resets.push_back(cr);
                        }
                    }
                }

                // Traceability requirements
                std::unordered_set<std::string> seen_reqs;
                for (const auto& edge : current_path) {
                    for (const auto& req : edge.traceability_reqs) {
                        if (seen_reqs.insert(req).second) {
                            composite.traceability_reqs.push_back(req);
                        }
                    }
                }

                composite.id = compute_deterministic_id(composite.source + "->" + composite.target + ":" +
                                                        composite.event + "[" + composite.guard.value_or("") + "]");
                synthesized_transitions.push_back(std::move(composite));
                return;
            }

            // Current destination is a junction: continue expanding outgoing transitions
            auto out_it = outgoing_map.find(current_dest);
            if (out_it == outgoing_map.end()) {
                // Dead end at junction: no valid outgoing transition
                return;
            }

            for (const auto& next_edge : out_it->second) {
                current_path.push_back(next_edge);
                explore_paths(current_path);
                current_path.pop_back();
            }
        };

    // Find all transitions originating from non-junction states that enter a junction
    for (const auto& t : ir.transitions) {
        if (junction_names.count(t.source) == 0 && junction_names.count(t.target) > 0) {
            std::vector<TransitionEdge> path;
            path.push_back(t);
            explore_paths(path);
        }
    }

    // 5. Remove all intermediate transitions touching junction nodes
    ir.transitions.erase(
        std::remove_if(ir.transitions.begin(), ir.transitions.end(),
                       [&](const TransitionEdge& t) {
                           return junction_names.count(t.source) > 0 || junction_names.count(t.target) > 0;
                       }),
        ir.transitions.end());

    // 6. Insert all synthesized composite transitions
    for (auto& st : synthesized_transitions) {
        ir.transitions.push_back(std::move(st));
    }

    // 7. Remove junction states from ir.states and choice_nodes
    ir.states.erase(std::remove_if(ir.states.begin(), ir.states.end(),
                                   [&](const StateNode& s) { return junction_names.count(s.name) > 0; }),
                    ir.states.end());

    ir.choice_nodes.erase(std::remove_if(ir.choice_nodes.begin(), ir.choice_nodes.end(),
                                         [&](const ChoiceNodeModel& c) { return junction_names.count(c.name) > 0; }),
                          ir.choice_nodes.end());

    return true;
}

}  // namespace fsm::middleend::passes

#pragma once

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

/**
 * @brief Target-Agnostic Middle-End Pass: Choice & Junction Pseudostate Inlining.
 *
 * Flattens dynamic branch evaluation nodes (Choice & Junction) directly on the Formal IR.
 * For each incoming transition I (from source S with event E, guard G_I, action A_I)
 * and each outgoing branch O (to target T with guard G_O, action A_O), generates the
 * composite inlined transition S -> T with merged guard (G_I && G_O) and sequenced actions (A_I; A_O).
 * Removes the choice pseudostates and intermediate edges from the model.
 */
class ChoiceInliningPass {
  public:
    [[nodiscard]] static std::string name() { return "ChoiceInlining"; }
    [[nodiscard]] static std::string description() {
        return "Inlines choice and junction pseudostates into direct composite transitions on the IR graph";
    }

    static bool run(FsmIr& ir, DiagnosticEngine& /*diag*/) {
        // Collect all choice/junction state names
        std::unordered_set<std::string> choice_names;
        for (const auto& c : ir.choice_nodes) {
            choice_names.insert(c.name);
        }
        for (const auto& s : ir.states) {
            if (s.kind == StateKind::Choice || s.kind == StateKind::Junction) {
                choice_names.insert(s.name);
            }
        }

        if (choice_names.empty()) {
            return true;
        }

        bool modified = false;

        // Iterate over choice nodes to inline
        for (const auto& choice_name : choice_names) {
            std::vector<TransitionEdge> incoming;
            std::vector<TransitionEdge> outgoing;

            for (const auto& t : ir.transitions) {
                if (t.target == choice_name) {
                    incoming.push_back(t);
                } else if (t.source == choice_name) {
                    outgoing.push_back(t);
                }
            }

            if (incoming.empty() || outgoing.empty()) {
                continue;
            }

            // Generate composite transitions
            std::vector<TransitionEdge> inlined_edges;
            for (const auto& in : incoming) {
                for (const auto& out : outgoing) {
                    TransitionEdge composite;
                    composite.source = in.source;
                    composite.source_ids = in.source_ids;
                    composite.target = out.target;
                    composite.target_ids = out.target_ids;
                    composite.event = in.event.empty() ? out.event : in.event;
                    composite.trigger = in.trigger;
                    composite.target_is_history = out.target_is_history;
                    composite.target_is_deep_history = out.target_is_deep_history;
                    composite.kind =
                        (in.source == out.target) ? TransitionEdgeKind::Internal : TransitionEdgeKind::External;
                    composite.priority = (out.priority > 0) ? out.priority : in.priority;

                    // Combine Guards
                    std::string combined_guard;
                    auto is_else = [](const std::optional<std::string>& g) {
                        return !g.has_value() || g->empty() || *g == "else" || *g == "otherwise" || *g == "default";
                    };

                    if (!is_else(in.guard) && !is_else(out.guard)) {
                        combined_guard = "fsm::and_<" + *in.guard + ", " + *out.guard + ">";
                    } else if (!is_else(out.guard)) {
                        combined_guard = *out.guard;
                    } else if (!is_else(in.guard)) {
                        combined_guard = *in.guard;
                    }

                    if (!combined_guard.empty()) {
                        composite.guard = combined_guard;
                    }

                    // Combine Actions
                    if (in.action.has_value() && !in.action->empty() && out.action.has_value() &&
                        !out.action->empty()) {
                        composite.action = *in.action + "_" + *out.action;
                    } else if (out.action.has_value() && !out.action->empty()) {
                        composite.action = out.action;
                    } else if (in.action.has_value() && !in.action->empty()) {
                        composite.action = in.action;
                    }

                    // Combine Action Signatures (assignments)
                    if (in.action_sig.has_value() || out.action_sig.has_value()) {
                        ActionSignature sig;
                        sig.name = composite.action.value_or("InlinedAction");
                        if (in.action_sig.has_value()) {
                            for (const auto& a : in.action_sig->assignments) {
                                sig.assignments.push_back(a);
                            }
                        }
                        if (out.action_sig.has_value()) {
                            for (const auto& a : out.action_sig->assignments) {
                                sig.assignments.push_back(a);
                            }
                        }
                        composite.action_sig = sig;
                    }

                    composite.id = compute_deterministic_id(composite.source + "->" + composite.target + ":" +
                                                            composite.event + "[" + composite.guard.value_or("") + "]");
                    inlined_edges.push_back(std::move(composite));
                }
            }

            // Remove original incoming & outgoing edges
            ir.transitions.erase(std::remove_if(ir.transitions.begin(), ir.transitions.end(),
                                                [&](const TransitionEdge& t) {
                                                    return t.target == choice_name || t.source == choice_name;
                                                }),
                                 ir.transitions.end());

            // Append inlined edges
            for (auto& edge : inlined_edges) {
                ir.transitions.push_back(std::move(edge));
            }

            // Remove choice node from states list and choice_nodes list
            ir.states.erase(std::remove_if(ir.states.begin(), ir.states.end(),
                                           [&](const StateNode& s) { return s.name == choice_name; }),
                            ir.states.end());
            ir.choice_nodes.erase(std::remove_if(ir.choice_nodes.begin(), ir.choice_nodes.end(),
                                                 [&](const ChoiceNodeModel& c) { return c.name == choice_name; }),
                                  ir.choice_nodes.end());

            modified = true;
        }

        return modified;
    }
};

}  // namespace fsm::middleend::passes

namespace fsm::middleend {
using passes::ChoiceInliningPass;
}  // namespace fsm::middleend

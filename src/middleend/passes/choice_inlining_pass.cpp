#include "fsm/middleend/passes/choice_inlining_pass.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

namespace {

bool is_plain_identifier_guard(const std::string& g) {
    if (g.empty()) {
        return false;
    }
    if (std::isalpha(static_cast<unsigned char>(g[0])) == 0 && g[0] != '_') {
        return false;
    }
    for (char c : g) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
            return false;
        }
    }
    return true;
}

std::string combine_choice_guards(const std::string& g1, const std::string& g2) {
    if (is_plain_identifier_guard(g1) && is_plain_identifier_guard(g2)) {
        return "fsm::and_<" + g1 + ", " + g2 + ">";
    }
    return "(" + g1 + ") && (" + g2 + ")";
}

}  // namespace

bool ChoiceInliningPass::run(FsmIr& ir, DiagnosticEngine& /*diag*/) {
    // Collect all choice/junction state names deterministically
    std::vector<std::string> choice_names;
    for (const auto& c : ir.choice_nodes) {
        if (std::find(choice_names.begin(), choice_names.end(), c.name) == choice_names.end()) {
            choice_names.push_back(c.name);
        }
    }
    for (const auto& s : ir.states) {
        if (s.kind == StateKind::Choice || s.kind == StateKind::Junction) {
            if (std::find(choice_names.begin(), choice_names.end(), s.name) == choice_names.end()) {
                choice_names.push_back(s.name);
            }
        }
    }

    if (choice_names.empty()) {
        return true;
    }

    bool modified = false;
    bool progress = true;
    std::size_t max_iterations = choice_names.size() + 1;
    std::size_t iter = 0;

    // Iterate over choice nodes until fixed point to handle chained choice nodes (C1 -> C2)
    while (progress && iter++ < max_iterations) {
        progress = false;
        for (const auto& choice_name : choice_names) {
            std::vector<TransitionEdge> incoming;
            std::vector<TransitionEdge> outgoing;
            bool has_self_loop = false;

            for (const auto& t : ir.transitions) {
                if (t.target == choice_name && t.source == choice_name) {
                    has_self_loop = true;
                } else if (t.target == choice_name) {
                    incoming.push_back(t);
                } else if (t.source == choice_name) {
                    outgoing.push_back(t);
                }
            }

            if (has_self_loop || incoming.empty() || outgoing.empty()) {
                continue;
            }

            // Generate composite transitions
            std::vector<TransitionEdge> inlined_edges;
            for (const auto& in : incoming) {
                for (const auto& out : outgoing) {
                    TransitionEdge composite;
                    composite.source = in.source;
                    composite.source_id = in.source_id.empty() ? compute_deterministic_id(in.source) : in.source_id;
                    composite.source_ids = in.source_ids;
                    composite.multi_source_ids = in.multi_source_ids;
                    composite.target = out.target;
                    composite.target_id = out.target_id.empty() ? compute_deterministic_id(out.target) : out.target_id;
                    composite.target_ids = out.target_ids;
                    composite.multi_target_ids = out.multi_target_ids;
                    composite.event = in.event.empty() ? out.event : in.event;
                    composite.trigger = !in.event.empty() || !std::holds_alternative<AnonymousTrigger>(in.trigger)
                                            ? in.trigger
                                            : out.trigger;
                    composite.target_is_history = out.target_is_history;
                    composite.target_is_deep_history = out.target_is_deep_history;
                    composite.kind = TransitionEdgeKind::External;
                    composite.priority = (out.priority > 0) ? out.priority : in.priority;

                    // Combine Guards
                    std::string combined_guard;
                    auto is_else = [](const std::optional<std::string>& g) {
                        return !g.has_value() || g->empty() || *g == "else" || *g == "otherwise" || *g == "default";
                    };

                    if (!is_else(in.guard) && !is_else(out.guard)) {
                        combined_guard = combine_choice_guards(*in.guard, *out.guard);
                    } else if (!is_else(out.guard)) {
                        combined_guard = *out.guard;
                    } else if (!is_else(in.guard)) {
                        combined_guard = *in.guard;
                    }

                    if (!combined_guard.empty()) {
                        composite.set_guard(combined_guard);
                    }

                    // Combine Condition Actions
                    if (in.condition_action.has_value() || out.condition_action.has_value()) {
                        std::string cond_name;
                        if (in.condition_action.has_value() && !in.condition_action->name.empty() &&
                            out.condition_action.has_value() && !out.condition_action->name.empty()) {
                            cond_name = in.condition_action->name + "_" + out.condition_action->name;
                        } else if (out.condition_action.has_value() && !out.condition_action->name.empty()) {
                            cond_name = out.condition_action->name;
                        } else if (in.condition_action.has_value() && !in.condition_action->name.empty()) {
                            cond_name = in.condition_action->name;
                        } else {
                            cond_name = "InlinedCondAction";
                        }
                        ActionSignature csig(cond_name);
                        std::vector<std::string> cond_invs;
                        if (in.condition_action.has_value()) {
                            csig.accepts_event = csig.accepts_event || in.condition_action->accepts_event;
                            if (!in.condition_action->invocation.empty() &&
                                in.condition_action->invocation != in.condition_action->name) {
                                cond_invs.push_back(in.condition_action->invocation);
                            }
                            for (const auto& a : in.condition_action->assignments) {
                                csig.assignments.push_back(a);
                            }
                            for (const auto& inst : in.condition_action->instructions) {
                                csig.instructions.push_back(inst);
                            }
                        }
                        if (out.condition_action.has_value()) {
                            csig.accepts_event = csig.accepts_event || out.condition_action->accepts_event;
                            if (!out.condition_action->invocation.empty() &&
                                out.condition_action->invocation != out.condition_action->name) {
                                cond_invs.push_back(out.condition_action->invocation);
                            }
                            for (const auto& a : out.condition_action->assignments) {
                                csig.assignments.push_back(a);
                            }
                            for (const auto& inst : out.condition_action->instructions) {
                                csig.instructions.push_back(inst);
                            }
                        }
                        if (!cond_invs.empty()) {
                            std::string inv_str;
                            for (std::size_t i = 0; i < cond_invs.size(); ++i) {
                                if (i > 0)
                                    inv_str += "; ";
                                inv_str += cond_invs[i];
                            }
                            csig.invocation = std::move(inv_str);
                        }
                        composite.condition_action = std::move(csig);
                    }

                    // Combine Transition Actions
                    std::string combined_act_name;
                    std::string in_act = in.get_action();
                    std::string out_act = out.get_action();
                    if (!in_act.empty() && !out_act.empty()) {
                        combined_act_name = in_act + "_" + out_act;
                    } else if (!out_act.empty()) {
                        combined_act_name = out_act;
                    } else if (!in_act.empty()) {
                        combined_act_name = in_act;
                    }

                    // Combine Action Signatures (assignments, instructions, invocation, accepts_event)
                    if (in.transition_action.has_value() || out.transition_action.has_value()) {
                        ActionSignature sig;
                        sig.name = combined_act_name.empty() ? "InlinedAction" : combined_act_name;
                        std::vector<std::string> trans_invs;
                        if (in.transition_action.has_value()) {
                            sig.accepts_event = sig.accepts_event || in.transition_action->accepts_event;
                            if (!in.transition_action->invocation.empty() &&
                                in.transition_action->invocation != in.transition_action->name) {
                                trans_invs.push_back(in.transition_action->invocation);
                            }
                            for (const auto& a : in.transition_action->assignments) {
                                sig.assignments.push_back(a);
                            }
                            for (const auto& inst : in.transition_action->instructions) {
                                sig.instructions.push_back(inst);
                            }
                        }
                        if (out.transition_action.has_value()) {
                            sig.accepts_event = sig.accepts_event || out.transition_action->accepts_event;
                            if (!out.transition_action->invocation.empty() &&
                                out.transition_action->invocation != out.transition_action->name) {
                                trans_invs.push_back(out.transition_action->invocation);
                            }
                            for (const auto& a : out.transition_action->assignments) {
                                sig.assignments.push_back(a);
                            }
                            for (const auto& inst : out.transition_action->instructions) {
                                sig.instructions.push_back(inst);
                            }
                        }
                        if (!trans_invs.empty()) {
                            std::string inv_str;
                            for (std::size_t i = 0; i < trans_invs.size(); ++i) {
                                if (i > 0)
                                    inv_str += "; ";
                                inv_str += trans_invs[i];
                            }
                            sig.invocation = std::move(inv_str);
                        } else {
                            sig.invocation = sig.name;
                        }
                        composite.transition_action = std::move(sig);
                    } else if (!combined_act_name.empty()) {
                        composite.transition_action = ActionSignature(combined_act_name);
                    }

                    // Combine clock resets
                    for (const auto& cr : in.clock_resets) {
                        if (std::find(composite.clock_resets.begin(), composite.clock_resets.end(), cr) ==
                            composite.clock_resets.end()) {
                            composite.clock_resets.push_back(cr);
                        }
                    }
                    for (const auto& cr : out.clock_resets) {
                        if (std::find(composite.clock_resets.begin(), composite.clock_resets.end(), cr) ==
                            composite.clock_resets.end()) {
                            composite.clock_resets.push_back(cr);
                        }
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
            progress = true;
        }
    }

    return modified;
}

}  // namespace fsm::middleend::passes

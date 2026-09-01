#pragma once

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class JsonSerializer {
  public:
    static std::string serialize(const FsmIr& model) {
        std::ostringstream out;
        out << "{\n";
        out << "  \"id\": \"" << (model.name.empty() ? "StateMachine" : model.name) << "\",\n";
        if (!model.initial_state.empty()) {
            out << "  \"initial\": \"" << model.initial_state << "\",\n";
        }

        // Ports
        if (!model.ports.empty()) {
            out << "  \"ports\": [\n";
            for (size_t p = 0; p < model.ports.size(); ++p) {
                const auto& port = model.ports[p];
                out << "    {\n";
                out << "      \"name\": \"" << escape_json(port.name) << "\",\n";
                out << "      \"type\": \"" << escape_json(port.type) << "\",\n";
                out << "      \"direction\": \""
                    << (port.is_out() ? "out" : (port.direction == PortDirection::InOut ? "inout" : "in")) << "\"";
                if (port.min_value.has_value()) {
                    out << ",\n      \"min\": " << *port.min_value;
                }
                if (port.max_value.has_value()) {
                    out << ",\n      \"max\": " << *port.max_value;
                }
                if (!port.constraint.empty()) {
                    out << ",\n      \"constraint\": \"" << escape_json(port.constraint) << "\"";
                }
                out << "\n    }";
                if (p + 1 < model.ports.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "  ],\n";
        }

        // Variables
        if (!model.variables.empty()) {
            out << "  \"variables\": [\n";
            for (size_t v = 0; v < model.variables.size(); ++v) {
                const auto& var = model.variables[v];
                out << "    {\n";
                out << "      \"name\": \"" << escape_json(var.name) << "\",\n";
                out << "      \"type\": \"" << escape_json(var.type) << "\",\n";
                out << "      \"init\": \"" << escape_json(var.initial_value) << "\"";
                if (!var.description.empty()) {
                    out << ",\n      \"description\": \"" << escape_json(var.description) << "\"";
                }
                out << "\n    }";
                if (v + 1 < model.variables.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "  ],\n";
        }

        // Signals
        if (!model.signals.empty()) {
            out << "  \"signals\": [\n";
            for (size_t s = 0; s < model.signals.size(); ++s) {
                const auto& sig = model.signals[s];
                out << "    {\n";
                out << "      \"name\": \"" << escape_json(sig.name) << "\"";
                if (!sig.attributes.empty()) {
                    out << ",\n      \"attributes\": [\n";
                    for (size_t a = 0; a < sig.attributes.size(); ++a) {
                        const auto& attr = sig.attributes[a];
                        out << "        { \"name\": \"" << escape_json(attr.name) << "\", \"type\": \""
                            << escape_json(attr.type) << "\" }";
                        if (a + 1 < sig.attributes.size()) {
                            out << ",";
                        }
                        out << "\n";
                    }
                    out << "      ]";
                }
                out << "\n    }";
                if (s + 1 < model.signals.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "  ],\n";
        }

        // Properties
        if (!model.properties.empty()) {
            out << "  \"properties\": [\n";
            for (size_t p = 0; p < model.properties.size(); ++p) {
                const auto& prop = model.properties[p];
                out << "    {\n";
                out << "      \"name\": \"" << escape_json(prop.name) << "\",\n";
                out << "      \"kind\": \"" << property_kind_to_string(prop.kind) << "\",\n";
                out << "      \"ltl\": \"" << escape_json(prop.raw_formula) << "\"";
                if (!prop.traceability_req.empty()) {
                    out << ",\n      \"req\": \"" << escape_json(prop.traceability_req) << "\"";
                }
                if (!prop.description.empty()) {
                    out << ",\n      \"desc\": \"" << escape_json(prop.description) << "\"";
                }
                out << "\n    }";
                if (p + 1 < model.properties.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "  ],\n";
        }

        out << "  \"states\": {\n";

        // Group top-level states
        std::vector<const StateNode*> top_states;
        for (const auto& s : model.states) {
            if (s.parent_state.empty()) {
                top_states.push_back(&s);
            }
        }

        for (size_t i = 0; i < top_states.size(); ++i) {
            emit_state(out, *top_states[i], model, 2);
            if (i + 1 < top_states.size()) {
                out << ",";
            }
            out << "\n";
        }

        out << "  }\n";
        out << "}\n";
        return out.str();
    }

  private:
    static void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model, size_t indent) {
        std::string pad(indent, ' ');
        out << pad << "\"" << state.name << "\": {\n";

        bool need_comma = false;

        // State kind
        if (state.kind != StateKind::Atomic) {
            if (need_comma) {
                out << ",\n";
            }
            out << pad << "  \"kind\": \"" << state_kind_to_string(state.kind) << "\"";
            need_comma = true;
        }

        // Time invariant
        if (state.time_invariant.has_value() && !state.time_invariant->empty()) {
            if (need_comma) {
                out << ",\n";
            }
            out << pad << "  \"time_invariant\": \"" << escape_json(*state.time_invariant) << "\"";
            need_comma = true;
        }

        // Initial sub-state if composite
        if (!state.initial_sub_state.empty()) {
            if (need_comma) {
                out << ",\n";
            }
            out << pad << "  \"initial\": \"" << state.initial_sub_state << "\"";
            need_comma = true;
        }

        // Entry actions
        if (!state.entry_actions.empty()) {
            if (need_comma) {
                out << ",\n";
            }
            out << pad << "  \"entry\": [";
            for (size_t a = 0; a < state.entry_actions.size(); ++a) {
                out << "\"" << escape_json(state.entry_actions[a].name) << "\"";
                if (a + 1 < state.entry_actions.size()) {
                    out << ", ";
                }
            }
            out << "]";
            need_comma = true;
        }

        // Exit actions
        if (!state.exit_actions.empty()) {
            if (need_comma) {
                out << ",\n";
            }
            out << pad << "  \"exit\": [";
            for (size_t a = 0; a < state.exit_actions.size(); ++a) {
                out << "\"" << escape_json(state.exit_actions[a].name) << "\"";
                if (a + 1 < state.exit_actions.size()) {
                    out << ", ";
                }
            }
            out << "]";
            need_comma = true;
        }

        // Do activity
        if (state.do_activity.has_value() && !state.do_activity->empty()) {
            if (need_comma) {
                out << ",\n";
            }
            out << pad << "  \"do\": \"" << escape_json(*state.do_activity) << "\"";
            need_comma = true;
        }

        // Traceability requirements
        if (!state.traceability_reqs.empty()) {
            if (need_comma) {
                out << ",\n";
            }
            out << pad << "  \"satisfies\": [";
            for (size_t r = 0; r < state.traceability_reqs.size(); ++r) {
                out << "\"" << escape_json(state.traceability_reqs[r]) << "\"";
                if (r + 1 < state.traceability_reqs.size()) {
                    out << ", ";
                }
            }
            out << "]";
            need_comma = true;
        }

        // Deferred events
        if (!state.deferred_events.empty()) {
            if (need_comma) {
                out << ",\n";
            }
            out << pad << "  \"defer\": [";
            for (size_t d = 0; d < state.deferred_events.size(); ++d) {
                out << "\"" << escape_json(state.deferred_events[d]) << "\"";
                if (d + 1 < state.deferred_events.size()) {
                    out << ", ";
                }
            }
            out << "]";
            need_comma = true;
        }

        // Outgoing transitions for this state (grouped by event preserving order)
        std::vector<std::string> event_order;
        std::map<std::string, std::vector<const TransitionEdge*>> grouped_trans;
        for (const auto& t : model.transitions) {
            if (t.source == state.name) {
                std::string evt = t.event.empty() ? "EVENT" : t.event;
                if (grouped_trans.find(evt) == grouped_trans.end()) {
                    event_order.push_back(evt);
                }
                grouped_trans[evt].push_back(&t);
            }
        }

        if (!event_order.empty()) {
            if (need_comma) {
                out << ",\n";
            }
            out << pad << "  \"on\": {\n";
            for (size_t g_idx = 0; g_idx < event_order.size(); ++g_idx) {
                const std::string& evt_name = event_order[g_idx];
                const auto& trans_list = grouped_trans[evt_name];
                if (trans_list.size() == 1) {
                    const auto* t = trans_list[0];
                    std::string target_str = t->target;
                    if (t->target_is_history) {
                        target_str += t->target_is_deep_history ? "[H*]" : "[H]";
                    }
                    out << pad << "    \"" << evt_name << "\": {\n";
                    out << pad << "      \"target\": \"" << target_str << "\"";
                    if (t->priority > 0) {
                        out << ",\n" << pad << "      \"priority\": " << t->priority;
                    }
                    if (t->guard && !t->guard->empty()) {
                        out << ",\n"
                            << pad << "      \"guard\": \""
                            << escape_json(GuardExpressionParser::to_diagram_string(*t->guard)) << "\"";
                    }
                    if (t->action && !t->action->empty()) {
                        out << ",\n" << pad << "      \"action\": \"" << escape_json(*t->action) << "\"";
                    }
                    out << "\n" << pad << "    }";
                } else {
                    out << pad << "    \"" << evt_name << "\": [\n";
                    for (size_t t_idx = 0; t_idx < trans_list.size(); ++t_idx) {
                        const auto* t = trans_list[t_idx];
                        std::string target_str = t->target;
                        if (t->target_is_history) {
                            target_str += t->target_is_deep_history ? "[H*]" : "[H]";
                        }
                        out << pad << "      {\n";
                        out << pad << "        \"target\": \"" << target_str << "\"";
                        if (t->priority > 0) {
                            out << ",\n" << pad << "        \"priority\": " << t->priority;
                        }
                        if (t->guard && !t->guard->empty()) {
                            out << ",\n"
                                << pad << "        \"guard\": \""
                                << escape_json(GuardExpressionParser::to_diagram_string(*t->guard)) << "\"";
                        }
                        if (t->action && !t->action->empty()) {
                            out << ",\n" << pad << "        \"action\": \"" << escape_json(*t->action) << "\"";
                        }
                        out << "\n" << pad << "      }";
                        if (t_idx + 1 < trans_list.size()) {
                            out << ",";
                        }
                        out << "\n";
                    }
                    out << pad << "    ]";
                }
                if (g_idx + 1 < event_order.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << pad << "  }";
            need_comma = true;
        }

        // Child states if composite
        std::vector<const StateNode*> child_states;
        for (const auto& s : model.states) {
            if (s.parent_state == state.name) {
                child_states.push_back(&s);
            }
        }

        if (!child_states.empty()) {
            if (need_comma) {
                out << ",\n";
            }
            out << pad << "  \"states\": {\n";
            for (size_t c_idx = 0; c_idx < child_states.size(); ++c_idx) {
                emit_state(out, *child_states[c_idx], model, indent + 4);
                if (c_idx + 1 < child_states.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << pad << "  }";
        }

        out << "\n" << pad << "}";
    }

    static std::string escape_json(const std::string& input) {
        std::string res;
        for (char c : input) {
            if (c == '"') {
                res += "\\\"";
            } else if (c == '\\') {
                res += "\\\\";
            } else {
                res += c;
            }
        }
        return res;
    }
};

}  // namespace fsm::codegen

#include "fsm/ir/fsm_ir_serializer.hpp"

#include <cctype>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/frontend/directive/guard_parser.hpp"

namespace fsm::ir {

namespace {

// ============================================================================
// JSON Escaping Helper
// ============================================================================

std::string escape_json(std::string_view s) {
    std::ostringstream ss;
    for (char c : s) {
        switch (c) {
            case '"':
                ss << "\\\"";
                break;
            case '\\':
                ss << "\\\\";
                break;
            case '\b':
                ss << "\\b";
                break;
            case '\f':
                ss << "\\f";
                break;
            case '\n':
                ss << "\\n";
                break;
            case '\r':
                ss << "\\r";
                break;
            case '\t':
                ss << "\\t";
                break;
            default:
                ss << c;
                break;
        }
    }
    return ss.str();
}

// ============================================================================
// State Serializer Helper
// ============================================================================

void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model, int indent) {
    std::string pad(indent, ' ');
    out << pad << "\"" << escape_json(state.name) << "\": {\n";

    bool need_comma = false;

    if (!state.entry_actions.empty()) {
        out << pad << "  \"entry\": [";
        for (size_t i = 0; i < state.entry_actions.size(); ++i) {
            out << "\"" << escape_json(state.entry_actions[i].name) << "\"";
            if (i + 1 < state.entry_actions.size()) {
                out << ", ";
            }
        }
        out << "]";
        need_comma = true;
    }

    if (!state.exit_actions.empty()) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"exit\": [";
        for (size_t i = 0; i < state.exit_actions.size(); ++i) {
            out << "\"" << escape_json(state.exit_actions[i].name) << "\"";
            if (i + 1 < state.exit_actions.size()) {
                out << ", ";
            }
        }
        out << "]";
        need_comma = true;
    }

    if (state.has_history) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"type\": \"history\",\n";
        out << pad << "  \"history\": \"" << (state.has_deep_history ? "deep" : "shallow") << "\"";
        need_comma = true;
    } else if (state.is_composite) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"type\": \"compound\"";
        if (!state.initial_sub_state.empty()) {
            out << ",\n" << pad << "  \"initial\": \"" << escape_json(state.initial_sub_state) << "\"";
        }
        need_comma = true;
    }

    if (state.kind != StateKind::Atomic) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"kind\": \"" << state_kind_to_string(state.kind) << "\"";
        need_comma = true;
    }

    if (state.time_invariant.has_value() && !state.time_invariant->empty()) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"time_invariant\": \"" << escape_json(state.time_invariant->to_string()) << "\"";
        need_comma = true;
    }

    if (state.do_activity.has_value()) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"do\": \"" << escape_json(*state.do_activity) << "\",\n";
        out << pad << "  \"do_activity\": \"" << escape_json(*state.do_activity) << "\"";
        need_comma = true;
    }

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
        out << "],\n";
        out << pad << "  \"traceability_reqs\": [";
        for (size_t r = 0; r < state.traceability_reqs.size(); ++r) {
            out << "\"" << escape_json(state.traceability_reqs[r]) << "\"";
            if (r + 1 < state.traceability_reqs.size()) {
                out << ", ";
            }
        }
        out << "]";
        need_comma = true;
    }

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
        out << "],\n";
        out << pad << "  \"deferred_events\": [";
        for (size_t d = 0; d < state.deferred_events.size(); ++d) {
            out << "\"" << escape_json(state.deferred_events[d]) << "\"";
            if (d + 1 < state.deferred_events.size()) {
                out << ", ";
            }
        }
        out << "]";
        need_comma = true;
    }

    if (!state.orthogonal_regions.empty()) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"orthogonal_regions\": [";
        for (size_t r = 0; r < state.orthogonal_regions.size(); ++r) {
            if (r > 0)
                out << ", ";
            const auto& reg = state.orthogonal_regions[r];
            out << "{\"id\": \"" << escape_json(reg.id) << "\", \"name\": \"" << escape_json(reg.name)
                << "\", \"initial_state_id\": \"" << escape_json(reg.initial_state_id) << "\", \"state_ids\": [";
            for (size_t s = 0; s < reg.state_ids.size(); ++s) {
                if (s > 0)
                    out << ", ";
                out << "\"" << escape_json(reg.state_ids[s]) << "\"";
            }
            out << "]}";
        }
        out << "]";
        need_comma = true;
    }

    if (state.submachine.has_value()) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"submachine\": {\"fsm_name\": \"" << escape_json(state.submachine->fsm_name)
            << "\", \"source_uri\": \"" << escape_json(state.submachine->source_uri) << "\", \"port_mappings\": [";
        for (size_t pm = 0; pm < state.submachine->port_mappings.size(); ++pm) {
            if (pm > 0)
                out << ", ";
            out << "{\"entry\": \"" << escape_json(state.submachine->port_mappings[pm].entry_point)
                << "\", \"exit\": \"" << escape_json(state.submachine->port_mappings[pm].exit_point) << "\"}";
        }
        out << "]}";
        need_comma = true;
    }

    // Outgoing transitions for this state
    std::vector<std::string> event_order;
    std::map<std::string, std::vector<const TransitionEdge*>> grouped_trans;
    for (const auto& t : model.transitions) {
        if (t.source == state.name) {
            std::string evt =
                (t.event.empty() || t.event == "Anonymous" || t.event == "AnonymousEvent" || t.event == "anonymous")
                    ? "always"
                    : t.event;
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
                        << escape_json(frontend::directive::GuardExpressionParser::to_diagram_string(*t->guard))
                        << "\"";
                }
                if (t->guard_ast.has_value()) {
                    out << ",\n" << pad << "      \"guard_ast\": \"" << escape_json(t->guard_ast->to_string()) << "\"";
                }
                std::string act = t->get_action();
                if (!act.empty()) {
                    out << ",\n"
                        << pad << "      \"action\": \"" << escape_json(act) << "\",\n"
                        << pad << "      \"action_sig\": \"" << escape_json(act) << "\"";
                }
                if (!t->source_ids.empty()) {
                    out << ",\n" << pad << "      \"source_ids\": [";
                    for (size_t s = 0; s < t->source_ids.size(); ++s) {
                        if (s > 0)
                            out << ", ";
                        out << "\"" << escape_json(t->source_ids[s]) << "\"";
                    }
                    out << "]";
                }
                if (!t->target_ids.empty()) {
                    out << ",\n" << pad << "      \"target_ids\": [";
                    for (size_t s = 0; s < t->target_ids.size(); ++s) {
                        if (s > 0)
                            out << ", ";
                        out << "\"" << escape_json(t->target_ids[s]) << "\"";
                    }
                    out << "]";
                }
                if (t->transition_action.has_value() && !t->transition_action->assignments.empty()) {
                    out << ",\n" << pad << "      \"assignments\": [";
                    for (size_t a = 0; a < t->transition_action->assignments.size(); ++a) {
                        if (a > 0)
                            out << ", ";
                        const auto& assign = t->transition_action->assignments[a];
                        out << "{\"variable\": \"" << escape_json(assign.target.name) << "\", \"target\": \""
                            << escape_json(assign.target.full_path()) << "\", \"op\": \""
                            << assignment_op_to_string(assign.op) << "\", \"expression\": \""
                            << escape_json(assign.expression) << "\"";
                        if (assign.expr_ast.has_value()) {
                            out << ", \"ast\": " << assign.expr_ast->to_json();
                        }
                        out << "}";
                    }
                    out << "]";
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
                            << escape_json(frontend::directive::GuardExpressionParser::to_diagram_string(*t->guard))
                            << "\"";
                    }
                    if (t->guard_ast.has_value()) {
                        out << ",\n"
                            << pad << "        \"guard_ast\": \"" << escape_json(t->guard_ast->to_string()) << "\"";
                    }
                    std::string t_act = t->get_action();
                    if (!t_act.empty()) {
                        out << ",\n"
                            << pad << "        \"action\": \"" << escape_json(t_act) << "\",\n"
                            << pad << "        \"action_sig\": \"" << escape_json(t_act) << "\"";
                    }
                    if (!t->source_ids.empty()) {
                        out << ",\n" << pad << "        \"source_ids\": [";
                        for (size_t s = 0; s < t->source_ids.size(); ++s) {
                            if (s > 0)
                                out << ", ";
                            out << "\"" << escape_json(t->source_ids[s]) << "\"";
                        }
                        out << "]";
                    }
                    if (!t->target_ids.empty()) {
                        out << ",\n" << pad << "        \"target_ids\": [";
                        for (size_t s = 0; s < t->target_ids.size(); ++s) {
                            if (s > 0)
                                out << ", ";
                            out << "\"" << escape_json(t->target_ids[s]) << "\"";
                        }
                        out << "]";
                    }
                    if (t->transition_action.has_value() && !t->transition_action->assignments.empty()) {
                        out << ",\n" << pad << "        \"assignments\": [";
                        for (size_t a = 0; a < t->transition_action->assignments.size(); ++a) {
                            if (a > 0)
                                out << ", ";
                            const auto& assign = t->transition_action->assignments[a];
                            out << "{\"variable\": \"" << escape_json(assign.target.name) << "\", \"target\": \""
                                << escape_json(assign.target.full_path()) << "\", \"op\": \""
                                << assignment_op_to_string(assign.op) << "\", \"expression\": \""
                                << escape_json(assign.expression) << "\"";
                            if (assign.expr_ast.has_value()) {
                                out << ", \"ast\": " << assign.expr_ast->to_json();
                            }
                            out << "}";
                        }
                        out << "]";
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

}  // namespace

// ============================================================================
// FsmIrSerializer Public API Implementation
// ============================================================================

std::string FsmIrSerializer::serialize_json(const FsmIr& ir, int indent_spaces) {
    (void)indent_spaces;
    std::ostringstream out;
    std::string sm_name = ir.name.empty() ? "StateMachine" : ir.name;
    out << "{\n";
    out << "  \"id\": \"" << sm_name << "\",\n";
    out << "  \"name\": \"" << sm_name << "\",\n";

    if (!ir.initial_state.empty()) {
        out << "  \"initial\": \"" << ir.initial_state << "\",\n";
    }

    if (!ir.package.empty()) {
        out << "  \"package\": \"" << escape_json(ir.package) << "\",\n";
    }

    // Attributes
    if (!ir.attributes.empty()) {
        out << "  \"attributes\": {";
        bool first_attr = true;
        for (const auto& [k, v] : ir.attributes) {
            if (!first_attr)
                out << ", ";
            first_attr = false;
            out << "\"" << escape_json(k) << "\": \"" << escape_json(v) << "\"";
        }
        out << "},\n";
    }

    // Concurrency
    out << "  \"concurrency\": {\n";
    out << "    \"dispatch\": \"" << event_dispatch_semantics_to_string(ir.concurrency.dispatch) << "\",\n";
    out << "    \"orthogonal_conflict\": \""
        << orthogonal_conflict_resolution_to_string(ir.concurrency.orthogonal_conflict) << "\",\n";
    out << "    \"datapath_isolation\": \"" << datapath_isolation_to_string(ir.concurrency.datapath_isolation)
        << "\"\n";
    out << "  },\n";

    // Ports
    if (!ir.ports.empty()) {
        out << "  \"ports\": [\n";
        for (size_t p = 0; p < ir.ports.size(); ++p) {
            const auto& port = ir.ports[p];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(port.name) << "\",\n";
            out << "      \"type\": \"" << escape_json(port.type.to_canonical_string()) << "\",\n";
            out << "      \"direction\": \""
                << (port.is_out() ? "out" : (port.direction == PortDirection::InOut ? "inout" : "in")) << "\"";
            if (port.min_value.has_value()) {
                out << ",\n      \"min\": " << *port.min_value;
                out << ",\n      \"min_value\": " << *port.min_value;
            }
            if (port.max_value.has_value()) {
                out << ",\n      \"max\": " << *port.max_value;
                out << ",\n      \"max_value\": " << *port.max_value;
            }
            if (!port.constraint.empty()) {
                out << ",\n      \"constraint\": \"" << escape_json(port.constraint) << "\"";
            }
            if (port.physical_unit.has_value()) {
                out << ",\n      \"physical_unit\": \"" << escape_json(*port.physical_unit) << "\"";
            }
            if (!port.description.empty()) {
                out << ",\n      \"description\": \"" << escape_json(port.description) << "\"";
            }
            out << "\n    }";
            if (p + 1 < ir.ports.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
    }

    // Variables
    if (!ir.variables.empty()) {
        out << "  \"variables\": [\n";
        for (size_t v = 0; v < ir.variables.size(); ++v) {
            const auto& var = ir.variables[v];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(var.name) << "\",\n";
            out << "      \"type\": \"" << escape_json(var.type.to_canonical_string()) << "\",\n";
            out << "      \"init\": \"" << escape_json(var.initial_value) << "\",\n";
            out << "      \"initial_value\": \"" << escape_json(var.initial_value) << "\"";
            if (var.min_value.has_value()) {
                out << ",\n      \"min_value\": " << *var.min_value;
            }
            if (var.max_value.has_value()) {
                out << ",\n      \"max_value\": " << *var.max_value;
            }
            if (var.physical_unit.has_value()) {
                out << ",\n      \"physical_unit\": \"" << escape_json(*var.physical_unit) << "\"";
            }
            if (!var.description.empty()) {
                out << ",\n      \"description\": \"" << escape_json(var.description) << "\"";
            }
            out << "\n    }";
            if (v + 1 < ir.variables.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
    }

    // Enums
    const auto enums = ir.get_enums();
    if (!enums.empty()) {
        out << "  \"enums\": [\n";
        for (size_t e = 0; e < enums.size(); ++e) {
            const auto& en = enums[e];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(en.name) << "\",\n";
            out << "      \"type\": \"" << escape_json(en.underlying_type) << "\",\n";
            out << "      \"underlying_type\": \"" << escape_json(en.underlying_type) << "\",\n";
            out << "      \"description\": \"" << escape_json(en.description) << "\",\n";
            out << "      \"literals\": [\n";
            for (size_t l = 0; l < en.literals.size(); ++l) {
                const auto& lit = en.literals[l];
                out << "        { \"name\": \"" << escape_json(lit.name) << "\"";
                if (lit.value.has_value()) {
                    out << ", \"value\": " << *lit.value;
                }
                if (!lit.description.empty()) {
                    out << ", \"description\": \"" << escape_json(lit.description) << "\"";
                }
                out << " }";
                if (l + 1 < en.literals.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "      ]\n";
            out << "    }";
            if (e + 1 < enums.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
    }

    // Structs
    const auto structs = ir.get_structs();
    if (!structs.empty()) {
        out << "  \"structs\": [\n";
        for (size_t st_i = 0; st_i < structs.size(); ++st_i) {
            const auto& st = structs[st_i];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(st.name) << "\",\n";
            out << "      \"is_datatype\": " << (st.is_datatype ? "true" : "false") << ",\n";
            out << "      \"description\": \"" << escape_json(st.description) << "\",\n";
            out << "      \"fields\": [\n";
            for (size_t f = 0; f < st.fields.size(); ++f) {
                const auto& field = st.fields[f];
                out << "        {\n";
                out << "          \"name\": \"" << escape_json(field.name) << "\",\n";
                out << "          \"type\": \"" << escape_json(field.type.to_canonical_string()) << "\",\n";
                out << "          \"default\": \"" << escape_json(field.default_value) << "\",\n";
                out << "          \"default_value\": \"" << escape_json(field.default_value) << "\"";
                if (field.physical_unit.has_value()) {
                    out << ",\n          \"physical_unit\": \"" << escape_json(*field.physical_unit) << "\"";
                }
                if (field.min_value.has_value()) {
                    out << ",\n          \"min_value\": " << *field.min_value;
                }
                if (field.max_value.has_value()) {
                    out << ",\n          \"max_value\": " << *field.max_value;
                }
                if (!field.description.empty()) {
                    out << ",\n          \"description\": \"" << escape_json(field.description) << "\"";
                }
                out << "\n        }";
                if (f + 1 < st.fields.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "      ]\n";
            out << "    }";
            if (st_i + 1 < structs.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
    }

    // Compound Types
    if (!ir.custom_types.empty()) {
        out << "  \"types\": [\n";
        for (size_t ti = 0; ti < ir.custom_types.size(); ++ti) {
            const auto& t = ir.custom_types[ti];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(t.name) << "\",\n";
            out << "      \"kind\": \"" << type_kind_to_string(t.kind) << "\",\n";
            out << "      \"underlying_type\": \"" << escape_json(t.underlying_type) << "\",\n";
            out << "      \"is_datatype\": " << (t.is_datatype ? "true" : "false");
            if (!t.literals.empty()) {
                out << ",\n      \"literals\": [\n";
                for (size_t li = 0; li < t.literals.size(); ++li) {
                    const auto& lit = t.literals[li];
                    out << "        { \"name\": \"" << escape_json(lit.name) << "\"";
                    if (lit.value.has_value()) {
                        out << ", \"value\": " << *lit.value;
                    }
                    out << " }";
                    if (li + 1 < t.literals.size())
                        out << ",";
                    out << "\n";
                }
                out << "      ]";
            }
            if (!t.fields.empty()) {
                out << ",\n      \"fields\": [\n";
                for (size_t fi = 0; fi < t.fields.size(); ++fi) {
                    const auto& f = t.fields[fi];
                    out << "        { \"name\": \"" << escape_json(f.name) << "\", \"type\": \""
                        << escape_json(f.type.to_canonical_string()) << "\"";
                    if (!f.default_value.empty()) {
                        out << ", \"default\": \"" << escape_json(f.default_value) << "\"";
                    }
                    out << " }";
                    if (fi + 1 < t.fields.size())
                        out << ",";
                    out << "\n";
                }
                out << "      ]";
            }
            out << "\n    }";
            if (ti + 1 < ir.custom_types.size())
                out << ",";
            out << "\n";
        }
        out << "  ],\n";
    }

    // Signals
    if (!ir.signals.empty()) {
        out << "  \"signals\": [\n";
        for (size_t s = 0; s < ir.signals.size(); ++s) {
            const auto& sig = ir.signals[s];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(sig.name) << "\"";
            if (!sig.attributes.empty()) {
                out << ",\n      \"attributes\": [\n";
                for (size_t a = 0; a < sig.attributes.size(); ++a) {
                    const auto& attr = sig.attributes[a];
                    out << "        { \"name\": \"" << escape_json(attr.name) << "\", \"type\": \""
                        << escape_json(attr.type.to_canonical_string()) << "\" }";
                    if (a + 1 < sig.attributes.size()) {
                        out << ",";
                    }
                    out << "\n";
                }
                out << "      ]";
            }
            if (!sig.validators.empty()) {
                out << ",\n      \"validators\": [";
                for (size_t v = 0; v < sig.validators.size(); ++v) {
                    if (v > 0)
                        out << ", ";
                    out << "\"" << escape_json(sig.validators[v]) << "\"";
                }
                out << "]";
            }
            out << "\n    }";
            if (s + 1 < ir.signals.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
    }

    // Properties
    if (!ir.properties.empty()) {
        out << "  \"properties\": [\n";
        for (size_t p = 0; p < ir.properties.size(); ++p) {
            const auto& prop = ir.properties[p];
            out << "    {\n";
            out << "      \"id\": \"" << escape_json(prop.id) << "\",\n";
            out << "      \"name\": \"" << escape_json(prop.name) << "\",\n";
            out << "      \"kind\": \"" << property_kind_to_string(prop.kind) << "\",\n";
            out << "      \"raw_formula\": \"" << escape_json(prop.raw_formula) << "\",\n";
            out << "      \"ltl\": \"" << escape_json(prop.raw_formula) << "\",\n";
            if (prop.ast.has_value()) {
                out << "      \"ast\": \"" << escape_json(prop.ast->to_string()) << "\",\n";
            }
            if (!prop.traceability_req.empty()) {
                out << "      \"traceability_req\": \"" << escape_json(prop.traceability_req) << "\",\n";
                out << "      \"req\": \"" << escape_json(prop.traceability_req) << "\",\n";
            }
            out << "      \"description\": \"" << escape_json(prop.description) << "\"\n";
            out << "    }";
            if (p + 1 < ir.properties.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
    }

    // States
    out << "  \"states\": {\n";
    std::vector<const StateNode*> top_states;
    for (const auto& s : ir.states) {
        if (s.parent_state.empty() && (!s.parent_id.has_value() || s.parent_id->empty())) {
            top_states.push_back(&s);
        }
    }
    for (size_t i = 0; i < top_states.size(); ++i) {
        emit_state(out, *top_states[i], ir, 4);
        if (i + 1 < top_states.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  }";
    if (!ir.transitions.empty()) {
        out << ",\n  \"transitions\": [\n";
        for (size_t i = 0; i < ir.transitions.size(); ++i) {
            const auto& tr = ir.transitions[i];
            out << "    {\n";
            out << "      \"id\": \"" << escape_json(tr.id) << "\",\n";
            out << "      \"source\": \"" << escape_json(tr.source) << "\",\n";
            out << "      \"target\": \"" << escape_json(tr.target) << "\",\n";
            out << "      \"source_ids\": [";
            for (size_t s = 0; s < tr.source_ids.size(); ++s) {
                if (s > 0)
                    out << ", ";
                out << "\"" << escape_json(tr.source_ids[s]) << "\"";
            }
            out << "],\n";
            out << "      \"target_ids\": [";
            for (size_t t = 0; t < tr.target_ids.size(); ++t) {
                if (t > 0)
                    out << ", ";
                out << "\"" << escape_json(tr.target_ids[t]) << "\"";
            }
            out << "],\n";
            out << "      \"kind\": \"" << transition_edge_kind_to_string(tr.kind) << "\",\n";
            out << "      \"priority\": " << tr.priority << ",\n";
            out << "      \"trigger\": \"" << escape_json(tr.get_trigger_name()) << "\",\n";
            if (tr.guard.has_value()) {
                std::string diag_g = frontend::directive::GuardExpressionParser::to_diagram_string(*tr.guard);
                out << "      \"guard\": \"" << escape_json(diag_g) << "\",\n";
                out << "      \"guard_ast\": \""
                    << escape_json(tr.guard_ast.has_value() ? tr.guard_ast->to_string() : diag_g) << "\",\n";
            } else if (tr.guard_ast.has_value()) {
                out << "      \"guard_ast\": \"" << escape_json(tr.guard_ast->to_string()) << "\",\n";
            }
            if (tr.transition_action.has_value()) {
                const auto& act = *tr.transition_action;
                out << "      \"action\": \"" << escape_json(act.name) << "\",\n";
                out << "      \"action_sig\": \"" << escape_json(act.name) << "\",\n";
                out << "      \"transition_action\": \"" << escape_json(act.name) << "\",\n";
                out << "      \"assignments\": [";
                for (size_t a = 0; a < act.assignments.size(); ++a) {
                    if (a > 0)
                        out << ", ";
                    const auto& assign = act.assignments[a];
                    out << "{\"variable\": \"" << escape_json(assign.target.name) << "\", \"target\": \""
                        << escape_json(assign.target.full_path()) << "\", \"op\": \""
                        << assignment_op_to_string(assign.op) << "\", \"expression\": \""
                        << escape_json(assign.expression) << "\"";
                    if (assign.expr_ast.has_value()) {
                        out << ", \"ast\": " << assign.expr_ast->to_json();
                    }
                    out << "}";
                }
                out << "]\n";
            } else {
                out << "      \"assignments\": []\n";
            }
            out << "    }" << (i + 1 < ir.transitions.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
    } else {
        out << "\n";
    }
    out << "}\n";
    return out.str();
}

}  // namespace fsm::ir

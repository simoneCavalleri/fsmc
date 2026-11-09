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
// Action & Transition Helpers
// ============================================================================

void emit_assignments_array(std::ostream& out, const std::vector<ActionAssignment>& assignments) {
    out << "[";
    for (size_t a = 0; a < assignments.size(); ++a) {
        if (a > 0)
            out << ", ";
        const auto& assign = assignments[a];
        out << "{\"variable\": \"" << escape_json(assign.target.name) << "\", \"target\": \""
            << escape_json(assign.target.full_path()) << "\", \"scoped_target\": \""
            << escape_json(assign.target.scoped_path()) << "\", \"scope\": \""
            << lvalue_scope_to_string(assign.target.scope) << "\", \"op\": \""
            << assignment_op_to_string(assign.op) << "\", \"expression\": \""
            << escape_json(assign.expression) << "\"";
        if (assign.expr_ast.has_value()) {
            out << ", \"ast\": " << assign.expr_ast->to_json();
        }
        out << "}";
    }
    out << "]";
}

void emit_action_signature_object(std::ostream& out, const ActionSignature& act) {
    out << "{\"name\": \"" << escape_json(act.name) << "\", \"invocation\": \"" << escape_json(act.invocation)
        << "\", \"accepts_event\": " << (act.accepts_event ? "true" : "false") << ", \"assignments\": ";
    emit_assignments_array(out, act.assignments);
    out << "}";
}

void emit_transition_metadata(std::ostream& out, const TransitionEdge& t, const std::string& field_pad,
                              bool is_top_level = false) {
    if (!is_top_level && !t.id.empty()) {
        out << ",\n" << field_pad << "\"id\": \"" << escape_json(t.id) << "\"";
    }
    if (!is_top_level && t.kind != TransitionEdgeKind::External) {
        out << ",\n" << field_pad << "\"kind\": \"" << transition_edge_kind_to_string(t.kind) << "\"";
    }
    if (!is_top_level && t.priority > 0) {
        out << ",\n" << field_pad << "\"priority\": " << t.priority;
    }
    if (t.target_is_history) {
        out << ",\n" << field_pad << "\"target_is_history\": true";
        out << ",\n" << field_pad << "\"target_is_deep_history\": " << (t.target_is_deep_history ? "true" : "false");
    }
    if (!t.description.empty()) {
        out << ",\n" << field_pad << "\"description\": \"" << escape_json(t.description) << "\"";
    }
    if (t.guard && !t.guard->empty()) {
        std::string diag_guard = frontend::directive::GuardExpressionParser::to_diagram_string(*t.guard);
        out << ",\n"
            << field_pad << "\"guard\": \""
            << escape_json(diag_guard) << "\"";
        if (*t.guard != diag_guard && t.guard->find("fsm::") == std::string::npos) {
            out << ",\n" << field_pad << "\"raw_guard\": \"" << escape_json(*t.guard) << "\"";
        }
        if (t.guard_ast.has_value()) {
            out << ",\n" << field_pad << "\"guard_ast\": \"" << escape_json(t.guard_ast->to_string()) << "\"";
        }
    } else if (t.guard_ast.has_value()) {
        out << ",\n" << field_pad << "\"guard_ast\": \"" << escape_json(t.guard_ast->to_string()) << "\"";
    }
    if (std::holds_alternative<TimeTrigger>(t.trigger)) {
        const auto& tt = std::get<TimeTrigger>(t.trigger);
        out << ",\n"
            << field_pad << "\"time_trigger\": {\"kind\": \"" << time_trigger_kind_to_string(tt.kind)
            << "\", \"duration_value\": " << tt.duration_value << ", \"unit\": \"" << time_unit_to_string(tt.unit)
            << "\", \"duration_ms\": " << tt.duration_in_ms()
            << ", \"periodic\": " << (tt.is_periodic() ? "true" : "false") << ", \"dynamic_expression\": \""
            << escape_json(tt.dynamic_expression) << "\"}";
    } else if (std::holds_alternative<ChangeTrigger>(t.trigger)) {
        const auto& ct = std::get<ChangeTrigger>(t.trigger);
        out << ",\n"
            << field_pad << "\"change_trigger\": {\"raw_expression\": \"" << escape_json(ct.raw_expression)
            << "\", \"active_on_true\": " << (ct.active_on_true ? "true" : "false") << "}";
    } else if (std::holds_alternative<SignalTrigger>(t.trigger)) {
        const auto& st = std::get<SignalTrigger>(t.trigger);
        if (st.payload_binding != "payload" || !st.payload_type.empty() || !st.payload_parameters.empty()) {
            out << ",\n"
                << field_pad << "\"signal_trigger\": {\"signal_name\": \"" << escape_json(st.signal_name)
                << "\", \"payload_binding\": \"" << escape_json(st.payload_binding) << "\", \"payload_type\": \""
                << escape_json(st.payload_type) << "\", \"payload_parameters\": [";
            for (size_t p = 0; p < st.payload_parameters.size(); ++p) {
                if (p > 0)
                    out << ", ";
                out << "{\"name\": \"" << escape_json(st.payload_parameters[p].first) << "\", \"type\": \""
                    << escape_json(st.payload_parameters[p].second) << "\"}";
            }
            out << "]}";
        }
    } else if (std::holds_alternative<AnonymousTrigger>(t.trigger)) {
        const auto& at = std::get<AnonymousTrigger>(t.trigger);
        if (!at.is_completion || at.description != "completion") {
            out << ",\n"
                << field_pad << "\"anonymous_trigger\": {\"is_completion\": " << (at.is_completion ? "true" : "false")
                << ", \"description\": \"" << escape_json(at.description) << "\"}";
        }
    }
    std::string act = t.get_action();
    if (!act.empty()) {
        out << ",\n"
            << field_pad << "\"action\": \"" << escape_json(act) << "\",\n"
            << field_pad << "\"action_sig\": \"" << escape_json(act) << "\"";
        if (is_top_level && t.transition_action.has_value()) {
            out << ",\n" << field_pad << "\"transition_action\": \"" << escape_json(t.transition_action->name) << "\"";
        }
    }
    if (t.transition_action.has_value()) {
        out << ",\n" << field_pad << "\"transition_action_obj\": ";
        emit_action_signature_object(out, *t.transition_action);
    }
    if (t.condition_action.has_value()) {
        out << ",\n" << field_pad << "\"condition_action\": ";
        emit_action_signature_object(out, *t.condition_action);
    }
    if (!is_top_level && !t.source_ids.empty()) {
        out << ",\n" << field_pad << "\"source_ids\": [";
        for (size_t s = 0; s < t.source_ids.size(); ++s) {
            if (s > 0)
                out << ", ";
            out << "\"" << escape_json(t.source_ids[s]) << "\"";
        }
        out << "]";
    }
    if (!is_top_level && !t.target_ids.empty()) {
        out << ",\n" << field_pad << "\"target_ids\": [";
        for (size_t s = 0; s < t.target_ids.size(); ++s) {
            if (s > 0)
                out << ", ";
            out << "\"" << escape_json(t.target_ids[s]) << "\"";
        }
        out << "]";
    }
    if (!t.clock_resets.empty()) {
        out << ",\n" << field_pad << "\"clock_resets\": [";
        for (size_t c = 0; c < t.clock_resets.size(); ++c) {
            if (c > 0)
                out << ", ";
            out << "\"" << escape_json(t.clock_resets[c]) << "\"";
        }
        out << "]";
    }
    if (!t.traceability_reqs.empty()) {
        out << ",\n" << field_pad << "\"traceability_reqs\": [";
        for (size_t r = 0; r < t.traceability_reqs.size(); ++r) {
            if (r > 0)
                out << ", ";
            out << "\"" << escape_json(t.traceability_reqs[r]) << "\"";
        }
        out << "]";
    }
    if (t.transition_action.has_value() && !t.transition_action->assignments.empty()) {
        out << ",\n" << field_pad << "\"assignments\": ";
        emit_assignments_array(out, t.transition_action->assignments);
    }
}

// ============================================================================
// State Serializer Helper
// ============================================================================

void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model, int indent) {
    std::string pad(indent, ' ');
    out << pad << "\"" << escape_json(state.name) << "\": {\n";

    bool need_comma = false;

    if (!state.id.empty()) {
        out << pad << "  \"id\": \"" << escape_json(state.id) << "\"";
        need_comma = true;
    }

    if (!state.alias.empty()) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"alias\": \"" << escape_json(state.alias) << "\"";
        need_comma = true;
    }

    if (!state.description.empty()) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"description\": \"" << escape_json(state.description) << "\"";
        need_comma = true;
    }

    if (state.pinned_parent) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"pinned_parent\": true";
        need_comma = true;
    }

    if (!state.entry_actions.empty()) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"entry\": [";
        for (size_t i = 0; i < state.entry_actions.size(); ++i) {
            out << "\"" << escape_json(state.entry_actions[i].name) << "\"";
            if (i + 1 < state.entry_actions.size()) {
                out << ", ";
            }
        }
        out << "],\n";
        out << pad << "  \"entry_actions\": [";
        for (size_t i = 0; i < state.entry_actions.size(); ++i) {
            if (i > 0)
                out << ", ";
            emit_action_signature_object(out, state.entry_actions[i]);
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
        out << "],\n";
        out << pad << "  \"exit_actions\": [";
        for (size_t i = 0; i < state.exit_actions.size(); ++i) {
            if (i > 0)
                out << ", ";
            emit_action_signature_object(out, state.exit_actions[i]);
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
    }
    if (state.is_composite) {
        if (need_comma) {
            out << ",\n";
        }
        if (!state.has_history) {
            out << pad << "  \"type\": \"compound\",\n";
        }
        out << pad << "  \"is_composite\": true";
        if (!state.initial_sub_state.empty()) {
            out << ",\n" << pad << "  \"initial\": \"" << escape_json(state.initial_sub_state) << "\"";
        }
        need_comma = true;
    } else if (!state.initial_sub_state.empty()) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"initial\": \"" << escape_json(state.initial_sub_state) << "\"";
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

    if (!state.invariants.empty()) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"invariants\": [";
        for (size_t inv_i = 0; inv_i < state.invariants.size(); ++inv_i) {
            if (inv_i > 0)
                out << ", ";
            out << "\"" << escape_json(state.invariants[inv_i].to_string()) << "\"";
        }
        out << "]";
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
                out << pad << "    \"" << escape_json(evt_name) << "\": {\n";
                out << pad << "      \"target\": \"" << escape_json(target_str) << "\"";
                emit_transition_metadata(out, *t, pad + "      ");
                out << "\n" << pad << "    }";
            } else {
                out << pad << "    \"" << escape_json(evt_name) << "\": [\n";
                for (size_t t_idx = 0; t_idx < trans_list.size(); ++t_idx) {
                    const auto* t = trans_list[t_idx];
                    std::string target_str = t->target;
                    if (t->target_is_history) {
                        target_str += t->target_is_deep_history ? "[H*]" : "[H]";
                    }
                    out << pad << "      {\n";
                    out << pad << "        \"target\": \"" << escape_json(target_str) << "\"";
                    emit_transition_metadata(out, *t, pad + "        ");
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
        if (s.name != state.name &&
            (s.parent_state == state.name || (!state.fqn.empty() && s.parent_state == state.fqn) ||
             (s.parent_id.has_value() && !state.id.empty() && *s.parent_id == state.id))) {
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
    out << "  \"id\": \"" << escape_json(sm_name) << "\",\n";
    if (!ir.id.empty()) {
        out << "  \"fsm_id\": \"" << escape_json(ir.id) << "\",\n";
    }
    out << "  \"name\": \"" << escape_json(sm_name) << "\",\n";

    if (!ir.initial_state.empty()) {
        out << "  \"initial\": \"" << escape_json(ir.initial_state) << "\",\n";
    }
    if (!ir.initial_state_id.empty()) {
        out << "  \"initial_state_id\": \"" << escape_json(ir.initial_state_id) << "\",\n";
    }

    if (!ir.package.empty()) {
        out << "  \"package\": \"" << escape_json(ir.package) << "\",\n";
    }

    if (!ir.satisfies_reqs.empty()) {
        out << "  \"satisfies_reqs\": [";
        for (size_t r = 0; r < ir.satisfies_reqs.size(); ++r) {
            if (r > 0)
                out << ", ";
            out << "\"" << escape_json(ir.satisfies_reqs[r]) << "\"";
        }
        out << "],\n";
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
                << (port.direction == PortDirection::InOut ? "inout"
                                                           : (port.direction == PortDirection::Out ? "out" : "in"))
                << "\"";
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
            if (!port.default_value.empty()) {
                out << ",\n      \"default\": \"" << escape_json(port.default_value) << "\"";
                out << ",\n      \"default_value\": \"" << escape_json(port.default_value) << "\"";
            }
            if (port.physical_unit.has_value()) {
                out << ",\n      \"unit\": \"" << escape_json(*port.physical_unit) << "\"";
                out << ",\n      \"physical_unit\": \"" << escape_json(*port.physical_unit) << "\"";
            }
            if (!port.description.empty()) {
                out << ",\n      \"description\": \"" << escape_json(port.description) << "\"";
            }
            if (!port.bound_to.empty()) {
                out << ",\n      \"bound_to\": \"" << escape_json(port.bound_to) << "\"";
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
                out << ",\n      \"min\": " << *var.min_value;
                out << ",\n      \"min_value\": " << *var.min_value;
            }
            if (var.max_value.has_value()) {
                out << ",\n      \"max\": " << *var.max_value;
                out << ",\n      \"max_value\": " << *var.max_value;
            }
            if (var.physical_unit.has_value()) {
                out << ",\n      \"unit\": \"" << escape_json(*var.physical_unit) << "\"";
                out << ",\n      \"physical_unit\": \"" << escape_json(*var.physical_unit) << "\"";
            }
            if (var.register_index.has_value()) {
                out << ",\n      \"register_index\": " << *var.register_index;
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
            out << "      \"description\": \"" << escape_json(t.description) << "\",\n";
            out << "      \"is_datatype\": " << (t.is_datatype ? "true" : "false");
            if (!t.literals.empty()) {
                out << ",\n      \"literals\": [\n";
                for (size_t li = 0; li < t.literals.size(); ++li) {
                    const auto& lit = t.literals[li];
                    out << "        { \"name\": \"" << escape_json(lit.name) << "\"";
                    if (lit.value.has_value()) {
                        out << ", \"value\": " << *lit.value;
                    }
                    if (!lit.description.empty()) {
                        out << ", \"description\": \"" << escape_json(lit.description) << "\"";
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
                        out << ", \"default\": \"" << escape_json(f.default_value) << "\", \"default_value\": \""
                            << escape_json(f.default_value) << "\"";
                    }
                    if (f.physical_unit.has_value()) {
                        out << ", \"unit\": \"" << escape_json(*f.physical_unit) << "\", \"physical_unit\": \""
                            << escape_json(*f.physical_unit) << "\"";
                    }
                    if (f.min_value.has_value()) {
                        out << ", \"min\": " << *f.min_value << ", \"min_value\": " << *f.min_value;
                    }
                    if (f.max_value.has_value()) {
                        out << ", \"max\": " << *f.max_value << ", \"max_value\": " << *f.max_value;
                    }
                    if (!f.description.empty()) {
                        out << ", \"description\": \"" << escape_json(f.description) << "\"";
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
            if (!sig.description.empty()) {
                out << ",\n      \"description\": \"" << escape_json(sig.description) << "\"";
            }
            if (!sig.attributes.empty()) {
                out << ",\n      \"attributes\": [\n";
                for (size_t a = 0; a < sig.attributes.size(); ++a) {
                    const auto& attr = sig.attributes[a];
                    out << "        { \"name\": \"" << escape_json(attr.name) << "\", \"type\": \""
                        << escape_json(attr.type.to_canonical_string()) << "\"";
                    if (!attr.default_value.empty()) {
                        out << ", \"default\": \"" << escape_json(attr.default_value) << "\", \"default_value\": \""
                            << escape_json(attr.default_value) << "\"";
                    }
                    out << " }";
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
            out << "      \"event\": \"" << escape_json(tr.event) << "\",\n";
            out << "      \"trigger\": \"" << escape_json(tr.get_trigger_name()) << "\"";
            emit_transition_metadata(out, tr, "      ", true);
            if (!tr.transition_action.has_value() || tr.transition_action->assignments.empty()) {
                out << ",\n      \"assignments\": []\n";
            } else {
                out << "\n";
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

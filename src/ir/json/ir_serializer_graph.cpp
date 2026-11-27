#include <cctype>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>

#include "fsm/ir/guard.hpp"
#include "ir_serializer_internal.hpp"

namespace fsm::ir::json_detail {

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
            << lvalue_scope_to_string(assign.target.scope) << "\", \"op\": \"" << assignment_op_to_string(assign.op)
            << "\", \"expression\": \"" << escape_json(assign.expression) << "\"";
        if (assign.expr_ast.has_value()) {
            out << ", \"ast\": " << assign.expr_ast->to_json();
        }
        out << "}";
    }
    out << "]";
}

void emit_string_array(std::ostream& out, const std::vector<std::string>& items) {
    out << "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0)
            out << ", ";
        out << "\"" << escape_json(items[i]) << "\"";
    }
    out << "]";
}

void emit_instructions_array(std::ostream& out, const std::vector<ActionAstNode>& instructions) {
    out << "[";
    for (size_t i = 0; i < instructions.size(); ++i) {
        if (i > 0)
            out << ", ";
        const auto& inst = instructions[i];
        out << "{";
        if (const auto* store = std::get_if<StoreOp>(&inst.op)) {
            out << "\"kind\": \"store\", \"description\": \"" << escape_json(inst.description) << "\", \"target\": \""
                << escape_json(store->target.full_path()) << "\", \"scope\": \""
                << lvalue_scope_to_string(store->target.scope) << "\", \"op\": \"" << assignment_op_to_string(store->op)
                << "\", \"expression\": \"" << escape_json(store->expression) << "\"";
            if (store->expr_ast.has_value()) {
                out << ", \"has_expr_ast\": true";
            }
        } else if (const auto* pw = std::get_if<PortWriteOp>(&inst.op)) {
            out << "\"kind\": \"port_write\", \"description\": \"" << escape_json(inst.description)
                << "\", \"port_name\": \"" << escape_json(pw->port_name) << "\", \"expression\": \""
                << escape_json(pw->expression) << "\", \"is_latched\": " << (pw->is_latched ? "true" : "false");
            if (pw->expr_ast.has_value()) {
                out << ", \"has_expr_ast\": true";
            }
        } else if (const auto* pr = std::get_if<PortReadOp>(&inst.op)) {
            out << "\"kind\": \"port_read\", \"description\": \"" << escape_json(inst.description)
                << "\", \"port_name\": \"" << escape_json(pr->port_name) << "\", \"destination\": \""
                << escape_json(pr->destination.full_path()) << "\", \"scope\": \""
                << lvalue_scope_to_string(pr->destination.scope) << "\"";
        } else if (const auto* se = std::get_if<SignalEmitOp>(&inst.op)) {
            out << "\"kind\": \"signal_emit\", \"description\": \"" << escape_json(inst.description)
                << "\", \"signal_name\": \"" << escape_json(se->signal_name) << "\", \"arguments\": ";
            emit_string_array(out, se->arguments);
            if (se->target_port.has_value()) {
                out << ", \"target_port\": \"" << escape_json(*se->target_port) << "\"";
            }
        } else if (const auto* ac = std::get_if<ActionCallOp>(&inst.op)) {
            out << "\"kind\": \"action_call\", \"description\": \"" << escape_json(inst.description)
                << "\", \"function_name\": \"" << escape_json(ac->function_name) << "\", \"arguments\": ";
            emit_string_array(out, ac->arguments);
            if (ac->result_target.has_value() && !ac->result_target->empty()) {
                out << ", \"result_target\": \"" << escape_json(ac->result_target->full_path())
                    << "\", \"result_scope\": \"" << lvalue_scope_to_string(ac->result_target->scope) << "\"";
            }
            out << ", \"read_set\": ";
            emit_string_array(out, ac->read_set);
            out << ", \"write_set\": ";
            emit_string_array(out, ac->write_set);
        }
        out << "}";
    }
    out << "]";
}

void emit_action_signature_object(std::ostream& out, const ActionSignature& act) {
    out << "{\"name\": \"" << escape_json(act.name) << "\", \"invocation\": \"" << escape_json(act.invocation)
        << "\", \"accepts_event\": " << (act.accepts_event ? "true" : "false") << ", \"assignments\": ";
    emit_assignments_array(out, act.assignments);
    if (!act.instructions.empty()) {
        out << ", \"instructions\": ";
        emit_instructions_array(out, act.instructions);
    }
    out << "}";
}

void emit_transition_metadata(std::ostream& out, const TransitionEdge& t, const std::string& field_pad,
                              bool is_top_level) {
    if (!is_top_level && !t.id.empty()) {
        out << ",\n" << field_pad << "\"id\": \"" << escape_json(t.id) << "\"";
    }
    if (!is_top_level && t.kind != TransitionEdgeKind::External) {
        out << ",\n" << field_pad << "\"kind\": \"" << transition_edge_kind_to_string(t.kind) << "\"";
    }
    if (!is_top_level && t.priority > 0) {
        out << ",\n" << field_pad << "\"priority\": " << t.priority;
    }
    if (t.source_line > 0) {
        out << ",\n" << field_pad << "\"source_line\": " << t.source_line;
    }
    if (t.target_is_history) {
        out << ",\n" << field_pad << "\"target_is_history\": true";
        out << ",\n" << field_pad << "\"target_is_deep_history\": " << (t.target_is_deep_history ? "true" : "false");
    }
    if (!t.description.empty()) {
        out << ",\n" << field_pad << "\"description\": \"" << escape_json(t.description) << "\"";
    }
    if (t.guard && !t.guard->empty()) {
        std::string diag_guard = GuardExpressionParser::to_diagram_string(*t.guard);
        out << ",\n" << field_pad << "\"guard\": \"" << escape_json(diag_guard) << "\"";
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
    if (!t.skip_exit_actions.empty()) {
        out << ",\n" << field_pad << "\"skip_exit_actions\": [";
        for (size_t s = 0; s < t.skip_exit_actions.size(); ++s) {
            if (s > 0)
                out << ", ";
            out << "\"" << escape_json(t.skip_exit_actions[s]) << "\"";
        }
        out << "]";
    }
    if (!t.skip_entry_actions.empty()) {
        out << ",\n" << field_pad << "\"skip_entry_actions\": [";
        for (size_t s = 0; s < t.skip_entry_actions.size(); ++s) {
            if (s > 0)
                out << ", ";
            out << "\"" << escape_json(t.skip_entry_actions[s]) << "\"";
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
    if (state.source_line > 0) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"source_line\": " << state.source_line;
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

    if (state.do_activity_action.has_value() && !state.do_activity_action->empty()) {
        if (need_comma) {
            out << ",\n";
        }
        out << pad << "  \"do_activity_action\": ";
        emit_action_signature_object(out, *state.do_activity_action);
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

}  // namespace fsm::ir::json_detail

#include "fsm/ir/guard.hpp"
#include "fsm/ir/identifier_utils.hpp"
#include "ir_deserializer_internal.hpp"

namespace fsm::ir::json_detail {

void parse_single_transition_object(const JsonValue& trans_obj, const std::string& source_state,
                                    const std::string& event_name, FsmIr& model) {
    std::string target_raw = trans_obj.get_string("target");
    if (target_raw.empty()) {
        target_raw = trans_obj.get_string("target_state");
    }

    bool is_history = false;
    bool is_deep = false;
    std::string clean_target = target_raw;
    if (clean_target.size() > 4 && clean_target.rfind("[H*]") == clean_target.size() - 4) {
        is_history = true;
        is_deep = true;
        clean_target.erase(clean_target.size() - 4);
    } else if (clean_target.size() > 3 && clean_target.rfind("[H]") == clean_target.size() - 3) {
        is_history = true;
        is_deep = false;
        clean_target.erase(clean_target.size() - 3);
    }
    if (const auto* th = trans_obj.get_child("target_is_history")) {
        if (th->type == JsonType::Bool) {
            is_history = th->bool_val;
        }
    }
    if (const auto* tdh = trans_obj.get_child("target_is_deep_history")) {
        if (tdh->type == JsonType::Bool) {
            is_deep = tdh->bool_val;
        }
    }

    TransitionEdge trans;
    trans.id = trans_obj.get_string("id");
    trans.source = source_state;
    trans.target = sanitize_identifier(clean_target);
    trans.target_is_history = is_history;
    trans.target_is_deep_history = is_deep;
    trans.description = trans_obj.get_string("description");
    if (const auto* sl = trans_obj.get_child("source_line"); sl != nullptr && sl->type == JsonType::Number) {
        if (sl->num_val > 0) {
            trans.source_line = static_cast<std::uint32_t>(sl->num_val);
        }
    }

    std::string kind_str = trans_obj.get_string("kind");
    if (!kind_str.empty()) {
        trans.kind = transition_edge_kind_from_string(kind_str);
    } else if (const auto* reenter_val = trans_obj.get_child("reenter");
               reenter_val != nullptr && reenter_val->type == JsonType::Bool) {
        trans.kind = reenter_val->bool_val ? TransitionEdgeKind::External : TransitionEdgeKind::Internal;
    } else if (const auto* internal_val = trans_obj.get_child("internal");
               internal_val != nullptr && internal_val->type == JsonType::Bool) {
        trans.kind = internal_val->bool_val ? TransitionEdgeKind::Internal : TransitionEdgeKind::External;
    }

    if (event_name == "always" || event_name.empty() || event_name == "Anonymous" || event_name == "AnonymousEvent" ||
        event_name == "anonymous") {
        trans.event = "";
        trans.trigger = AnonymousTrigger{};
    } else {
        trans.event = sanitize_identifier(event_name);
        trans.trigger = SignalTrigger{trans.event};
    }

    std::string guard_str = trans_obj.get_string("guard");
    if (guard_str.empty()) {
        guard_str = trans_obj.get_string("cond");
    }
    if (guard_str.empty()) {
        guard_str = trans_obj.get_string("guard_ast");
    }
    std::optional<GuardAstNode> parsed_guard_ast;
    if (!guard_str.empty()) {
        auto parsed = GuardExpressionParser::parse(guard_str);
        if (!parsed.canonical_expression.empty()) {
            trans.guard = parsed.canonical_expression;
            parsed_guard_ast = std::move(parsed.ast);
            for (const auto& detail : parsed.atomic_guard_details) {
                model.add_guard(detail.name, "", detail.expression, detail.expression);
            }
            for (const auto& atomic : parsed.atomic_guards) {
                model.add_guard(atomic);
            }
        } else {
            trans.guard = guard_str;
            model.add_guard(guard_str);
        }
    }
    std::string raw_guard = trans_obj.get_string("raw_guard");
    if (!raw_guard.empty()) {
        trans.guard = raw_guard;
    }
    std::string guard_ast_str = trans_obj.get_string("guard_ast");
    if (!guard_ast_str.empty() && guard_ast_str.find("fsm::") != std::string::npos) {
        trans.guard_ast = GuardAstNode(guard_ast_str);
    } else if (parsed_guard_ast.has_value()) {
        trans.guard_ast = std::move(*parsed_guard_ast);
    } else if (trans.guard.has_value() && !trans.guard->empty()) {
        trans.guard_ast = GuardAstNode(*trans.guard);
    }

    if (const auto* tao = trans_obj.get_child("transition_action_obj")) {
        if (tao->is_object()) {
            ActionSignature sig = parse_action_signature_object(*tao);
            if (!sig.name.empty()) {
                model.add_action(sig.name);
            }
            trans.transition_action = std::move(sig);
        }
    } else {
        std::string act_str = trans_obj.get_string("action");
        if (act_str.empty()) {
            act_str = trans_obj.get_string("actions");
        }
        if (act_str.empty()) {
            act_str = trans_obj.get_string("transition_action");
        }
        if (act_str.empty()) {
            act_str = trans_obj.get_string("action_sig");
        }
        if (act_str.empty()) {
            const auto* act_val = trans_obj.get_child("actions");
            if (act_val != nullptr) {
                if (act_val->is_string()) {
                    act_str = act_val->str_val;
                } else if (act_val->is_array() && !act_val->arr_val.empty() && act_val->arr_val.front().is_string()) {
                    act_str = act_val->arr_val.front().str_val;
                }
            }
        }
        if (!act_str.empty()) {
            std::string prefix = sanitize_identifier(trans.source) + "_" + sanitize_identifier(trans.target) + "_act";
            if (auto assign_sig = try_parse_assignment_action(act_str, prefix)) {
                model.add_action(assign_sig->name);
                trans.transition_action = std::move(*assign_sig);
            } else {
                std::string act_san = sanitize_identifier(act_str);
                model.add_action(act_san);
                trans.transition_action = ActionSignature{act_san};
            }
        }
    }

    if (const auto* cao = trans_obj.get_child("condition_action")) {
        if (cao->is_object()) {
            ActionSignature sig = parse_action_signature_object(*cao);
            if (!sig.name.empty()) {
                model.add_action(sig.name);
            }
            trans.condition_action = std::move(sig);
        } else if (cao->is_string() && !cao->str_val.empty()) {
            std::string c_act = sanitize_identifier(cao->str_val);
            model.add_action(c_act);
            trans.condition_action = ActionSignature{c_act};
        }
    }

    if (trans_obj.get_child("priority") != nullptr) {
        trans.priority = static_cast<std::uint32_t>(trans_obj.get_number("priority", 0.0));
    }

    if (const auto* s_arr = trans_obj.get_child("source_ids")) {
        if (s_arr->is_array()) {
            for (const auto& elem : s_arr->arr_val) {
                if (elem.is_string())
                    trans.source_ids.push_back(elem.str_val);
            }
        }
    }
    if (const auto* t_arr = trans_obj.get_child("target_ids")) {
        if (t_arr->is_array()) {
            for (const auto& elem : t_arr->arr_val) {
                if (elem.is_string())
                    trans.target_ids.push_back(elem.str_val);
            }
        }
    }
    if (const auto* cr_arr = trans_obj.get_child("clock_resets")) {
        if (cr_arr->is_array()) {
            for (const auto& elem : cr_arr->arr_val) {
                if (elem.is_string())
                    trans.clock_resets.push_back(elem.str_val);
            }
        }
    }
    if (const auto* tr_arr = trans_obj.get_child("traceability_reqs")) {
        if (tr_arr->is_array()) {
            for (const auto& elem : tr_arr->arr_val) {
                if (elem.is_string())
                    trans.traceability_reqs.push_back(elem.str_val);
            }
        }
    }
    if (const auto* se_arr = trans_obj.get_child("skip_exit_actions")) {
        if (se_arr->is_array()) {
            for (const auto& elem : se_arr->arr_val) {
                if (elem.is_string())
                    trans.skip_exit_actions.push_back(elem.str_val);
            }
        }
    }
    if (const auto* sen_arr = trans_obj.get_child("skip_entry_actions")) {
        if (sen_arr->is_array()) {
            for (const auto& elem : sen_arr->arr_val) {
                if (elem.is_string())
                    trans.skip_entry_actions.push_back(elem.str_val);
            }
        }
    }

    // Structured triggers (time_trigger, change_trigger, signal_trigger, anonymous_trigger)
    if (const auto* tt_val = trans_obj.get_child("time_trigger")) {
        if (tt_val->is_object()) {
            TimeTrigger tt;
            std::string k = tt_val->get_string("kind", "after");
            tt.kind = time_trigger_kind_from_string(k);
            tt.duration_value =
                static_cast<uint64_t>(tt_val->get_number("duration_value", tt_val->get_number("duration_ms", 0.0)));
            std::string u = tt_val->get_string("unit", "ms");
            tt.unit = time_unit_from_string(u);
            tt.dynamic_expression = tt_val->get_string("dynamic_expression");
            if (tt_val->get_child("duration_ms") != nullptr) {
                tt.duration_ms = static_cast<uint64_t>(tt_val->get_number("duration_ms", 0.0));
            } else {
                tt.duration_ms = tt.duration_in_ms();
            }
            if (const auto* p_val = tt_val->get_child("periodic")) {
                if (p_val->type == JsonType::Bool) {
                    tt.periodic = p_val->bool_val;
                } else {
                    tt.periodic = (tt.kind == TimeTriggerKind::Every);
                }
            } else {
                tt.periodic = (tt.kind == TimeTriggerKind::Every);
            }
            trans.trigger = tt;
        }
    } else if (const auto* ct_val = trans_obj.get_child("change_trigger")) {
        if (ct_val->is_object()) {
            std::string raw_expr = ct_val->get_string("raw_expression");
            bool on_true = true;
            if (const auto* aot = ct_val->get_child("active_on_true")) {
                if (aot->type == JsonType::Bool) {
                    on_true = aot->bool_val;
                }
            }
            trans.trigger = ChangeTrigger(raw_expr, on_true);
        }
    } else if (const auto* st_val = trans_obj.get_child("signal_trigger")) {
        if (st_val->is_object()) {
            SignalTrigger st;
            st.signal_name = st_val->get_string("signal_name", trans.event);
            st.payload_binding = st_val->get_string("payload_binding", "payload");
            st.payload_type = st_val->get_string("payload_type");
            if (const auto* pp_arr = st_val->get_child("payload_parameters")) {
                if (pp_arr->is_array()) {
                    for (const auto& pp : pp_arr->arr_val) {
                        if (pp.is_object()) {
                            st.payload_parameters.emplace_back(pp.get_string("name"), pp.get_string("type"));
                        }
                    }
                }
            }
            trans.trigger = st;
        }
    } else if (const auto* at_val = trans_obj.get_child("anonymous_trigger")) {
        if (at_val->is_object()) {
            AnonymousTrigger at;
            if (const auto* ic = at_val->get_child("is_completion")) {
                if (ic->type == JsonType::Bool) {
                    at.is_completion = ic->bool_val;
                }
            }
            at.description = at_val->get_string("description", "completion");
            trans.trigger = at;
        }
    }

    // Parse assignments if transition_action_obj did not already populate them
    if (const auto* assigns_val = trans_obj.get_child("assignments")) {
        if (assigns_val->is_array() &&
            (!trans.transition_action.has_value() || trans.transition_action->assignments.empty())) {
            std::string act_name = trans.get_action().empty() ? "trans_action" : trans.get_action();
            ActionSignature act_sig = trans.transition_action.value_or(ActionSignature(act_name));
            for (const auto& a_val : assigns_val->arr_val) {
                if (a_val.is_object()) {
                    act_sig.assignments.push_back(parse_assignment_object(a_val));
                }
            }
            if (!act_sig.assignments.empty()) {
                trans.transition_action = std::move(act_sig);
            }
        }
    }

    const auto* src_node = model.find_state(source_state);
    trans.parent_scope = (src_node != nullptr) ? src_node->parent_state : "";
    if (trans.target.empty() && trans.target_ids.empty()) {
        trans.target = source_state;
        if (kind_str.empty()) {
            trans.kind = TransitionEdgeKind::Internal;
        }
    }
    if (!trans.target.empty() && !model.is_choice_node(trans.target)) {
        model.add_state(trans.target);
    }
    if (!trans.event.empty()) {
        model.add_event(trans.event);
    }
    if (trans.id.empty() && !trans.source.empty() && !trans.target.empty()) {
        trans.id = compute_deterministic_id(trans.source + "->" + trans.target + ":" + trans.event + "[" +
                                            (trans.guard ? *trans.guard : "") + "]");
    }
    if (!trans.source.empty() && trans.source_id.empty()) {
        if (const auto* resolved_src = model.find_state_by_name(trans.source)) {
            trans.source_id = resolved_src->id;
        } else {
            trans.source_id = compute_deterministic_id(trans.source);
        }
        if (trans.source_ids.empty()) {
            trans.source_ids.push_back(trans.source);
        }
        if (trans.multi_source_ids.empty()) {
            trans.multi_source_ids.push_back(trans.source_id);
        }
    }
    if (!trans.target.empty() && trans.target_id.empty()) {
        if (const auto* dst_node = model.find_state_by_name(trans.target)) {
            trans.target_id = dst_node->id;
        } else {
            trans.target_id = compute_deterministic_id(trans.target);
        }
        if (trans.target_ids.empty()) {
            trans.target_ids.push_back(trans.target);
        }
        if (trans.multi_target_ids.empty()) {
            trans.multi_target_ids.push_back(trans.target_id);
        }
    }
    model.add_transition(std::move(trans));
}

void parse_on_transitions(const JsonValue& on_obj, const std::string& source_state, FsmIr& model) {
    for (std::size_t i = 0; i < on_obj.obj_keys.size(); ++i) {
        const auto& evt_key = on_obj.obj_keys[i];
        const auto& trans_val = on_obj.obj_values[i];
        if (trans_val.is_object()) {
            parse_single_transition_object(trans_val, source_state, evt_key, model);
        } else if (trans_val.is_string()) {
            JsonValue fake_obj;
            fake_obj.type = JsonType::Object;
            fake_obj.set_member("target", trans_val);
            parse_single_transition_object(fake_obj, source_state, evt_key, model);
        } else if (trans_val.is_array()) {
            for (const auto& elem : trans_val.arr_val) {
                if (elem.is_object()) {
                    parse_single_transition_object(elem, source_state, evt_key, model);
                } else if (elem.is_string()) {
                    JsonValue fake_obj;
                    fake_obj.type = JsonType::Object;
                    fake_obj.set_member("target", elem);
                    parse_single_transition_object(fake_obj, source_state, evt_key, model);
                }
            }
        }
    }
}

}  // namespace fsm::ir::json_detail

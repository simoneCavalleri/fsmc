#include <cstdlib>

#include "fsm/ir/identifier_utils.hpp"
#include "ir_deserializer_internal.hpp"

namespace fsm::ir::json_detail {

void parse_states_object(const JsonValue& states_obj, FsmIr& model, const std::string& current_parent) {
    for (std::size_t i = 0; i < states_obj.obj_keys.size(); ++i) {
        parse_single_state_entry(states_obj.obj_keys[i], states_obj.obj_values[i], model, current_parent);
    }
}

void parse_single_state_entry(const std::string& state_key, const JsonValue& state_data, FsmIr& model,
                              const std::string& current_parent) {
    {
        const std::string state_name = sanitize_identifier(state_key);
        auto& node = model.add_state(state_name, current_parent);
        node.parent_state = current_parent;

        if (!current_parent.empty()) {
            auto* parent = model.find_state_mut(current_parent);
            if (parent != nullptr) {
                parent->is_composite = true;
                if (parent->initial_sub_state.empty()) {
                    parent->initial_sub_state = state_name;
                }
            }
        }

        if (!state_data.is_object()) {
            return;
        }

        auto* curr = model.find_state_mut(state_name);
        if (curr != nullptr) {
            std::string st_id = state_data.get_string("id");
            if (!st_id.empty()) {
                curr->id = st_id;
            }
            std::string st_alias = state_data.get_string("alias");
            if (!st_alias.empty()) {
                curr->alias = st_alias;
            }
            std::string st_desc = state_data.get_string("description");
            if (!st_desc.empty()) {
                curr->description = st_desc;
            }
            if (const auto* pp = state_data.get_child("pinned_parent")) {
                if (pp->type == JsonType::Bool) {
                    curr->pinned_parent = pp->bool_val;
                }
            }
        }

        auto parse_lifecycle_actions = [&](const char* primary_key, const char* fallback_key, const char* suffix,
                                           std::vector<ActionSignature>& target) {
            if (curr == nullptr) {
                return;
            }
            auto append_action_str = [&](const std::string& raw) {
                if (raw.empty()) {
                    return;
                }
                if (auto assign_sig = try_parse_assignment_action(raw, curr->name + suffix)) {
                    model.add_action(assign_sig->name);
                    target.push_back(std::move(*assign_sig));
                } else {
                    std::string act = sanitize_identifier(raw);
                    model.add_action(act);
                    target.push_back(ActionSignature{act});
                }
            };
            auto append_action_item = [&](const JsonValue& item) {
                if (item.is_object()) {
                    ActionSignature sig = parse_action_signature_object(item);
                    if (!sig.name.empty()) {
                        model.add_action(sig.name);
                    }
                    target.push_back(std::move(sig));
                } else if (item.is_string()) {
                    append_action_str(item.str_val);
                }
            };

            if (const auto* primary_val = state_data.get_child(primary_key);
                primary_val != nullptr && primary_val->is_array()) {
                for (const auto& item : primary_val->arr_val) {
                    append_action_item(item);
                }
            } else if (const auto* fallback_val = state_data.get_child(fallback_key)) {
                if (fallback_val->is_string()) {
                    append_action_str(fallback_val->str_val);
                } else if (fallback_val->is_array()) {
                    for (const auto& item : fallback_val->arr_val) {
                        append_action_item(item);
                    }
                }
            }
        };

        parse_lifecycle_actions("entry_actions", "entry", "_entry", curr->entry_actions);
        parse_lifecycle_actions("exit_actions", "exit", "_exit", curr->exit_actions);

        // Parse type / history / composite / initial
        std::string s_type = state_data.get_string("type");
        if (s_type == "history" && curr != nullptr) {
            curr->has_history = true;
            std::string h_mode = state_data.get_string("history", "shallow");
            curr->has_deep_history = (h_mode == "deep");
        }
        if (const auto* h_val = state_data.get_child("history")) {
            if (h_val->is_string() && curr != nullptr) {
                curr->has_history = true;
                curr->has_deep_history = (h_val->str_val == "deep");
            }
        }
        if (s_type == "compound" && curr != nullptr) {
            curr->is_composite = true;
        }
        if (s_type == "parallel" && curr != nullptr) {
            curr->is_composite = true;
            curr->kind = StateKind::Parallel;
        }
        if (s_type == "final" && curr != nullptr) {
            curr->kind = StateKind::Final;
        }
        if (const auto* ic_val = state_data.get_child("is_composite")) {
            if (ic_val->type == JsonType::Bool && curr != nullptr) {
                curr->is_composite = ic_val->bool_val;
            }
        }
        std::string sub_init = state_data.get_string("initial");
        if (!sub_init.empty() && curr != nullptr) {
            curr->initial_sub_state = sanitize_identifier(sub_init);
        }

        std::string s_kind = state_data.get_string("kind");
        if (!s_kind.empty() && curr != nullptr) {
            curr->kind = state_kind_from_string(s_kind);
        }
        if (const auto* sl = state_data.get_child("source_line");
            sl != nullptr && sl->type == JsonType::Number && curr != nullptr) {
            if (sl->num_val > 0) {
                curr->source_line = static_cast<std::uint32_t>(sl->num_val);
            }
        }
        std::string s_ti = state_data.get_string("time_invariant");
        if (!s_ti.empty() && curr != nullptr) {
            curr->time_invariant = StateTimeInvariant(s_ti);
        }
        if (const auto* inv_val = state_data.get_child("invariants")) {
            if (inv_val->is_array() && curr != nullptr) {
                for (const auto& item : inv_val->arr_val) {
                    if (item.is_string() && !item.str_val.empty()) {
                        curr->invariants.emplace_back(item.str_val);
                    }
                }
            }
        }
        if (const auto* reg_val = state_data.get_child("orthogonal_regions")) {
            if (reg_val->is_array() && curr != nullptr) {
                for (const auto& r_item : reg_val->arr_val) {
                    if (r_item.is_object()) {
                        OrthogonalRegion reg;
                        reg.id = r_item.get_string("id");
                        reg.name = r_item.get_string("name");
                        reg.initial_state_id = r_item.get_string("initial_state_id");
                        if (const auto* sids = r_item.get_child("state_ids")) {
                            if (sids->is_array()) {
                                for (const auto& sid : sids->arr_val) {
                                    if (sid.is_string()) {
                                        reg.state_ids.push_back(sid.str_val);
                                    }
                                }
                            }
                        }
                        curr->orthogonal_regions.push_back(std::move(reg));
                    }
                }
            }
        }
        if (const auto* sub_val = state_data.get_child("submachine")) {
            if (sub_val->is_object() && curr != nullptr) {
                SubmachineRef sub(sub_val->get_string("fsm_name"), sub_val->get_string("source_uri"));
                if (const auto* pms = sub_val->get_child("port_mappings")) {
                    if (pms->is_array()) {
                        for (const auto& pm : pms->arr_val) {
                            if (pm.is_object()) {
                                sub.port_mappings.emplace_back(pm.get_string("entry"), pm.get_string("exit"));
                            }
                        }
                    }
                }
                curr->submachine = std::move(sub);
            }
        }

        // Parse do activity
        std::string s_do = state_data.get_string("do");
        if (s_do.empty()) {
            s_do = state_data.get_string("do_activity");
        }
        if (!s_do.empty() && curr != nullptr) {
            curr->do_activity = s_do;
        }

        const auto* meta_obj = state_data.get_child("meta");
        if (meta_obj != nullptr && !meta_obj->is_object()) {
            meta_obj = nullptr;
        }

        // Parse satisfies / requirements (top-level or XState v5 meta.requirements)
        const auto* reqs_val = state_data.get_child("satisfies");
        if (reqs_val == nullptr) {
            reqs_val = state_data.get_child("requirements");
        }
        if (reqs_val == nullptr) {
            reqs_val = state_data.get_child("traceability_reqs");
        }
        if (reqs_val == nullptr && meta_obj != nullptr) {
            reqs_val = meta_obj->get_child("requirements");
            if (reqs_val == nullptr) {
                reqs_val = meta_obj->get_child("satisfies");
            }
        }
        if (reqs_val != nullptr && curr != nullptr) {
            if (reqs_val->is_string()) {
                curr->traceability_reqs.push_back(reqs_val->str_val);
            } else if (reqs_val->is_array()) {
                for (const auto& r_item : reqs_val->arr_val) {
                    if (r_item.is_string()) {
                        curr->traceability_reqs.push_back(r_item.str_val);
                    }
                }
            }
        }

        // Parse deferred events (top-level or XState v5 meta.deferred)
        const auto* def_val = state_data.get_child("defer");
        if (def_val == nullptr) {
            def_val = state_data.get_child("deferred_events");
        }
        if (def_val == nullptr && meta_obj != nullptr) {
            def_val = meta_obj->get_child("deferred");
            if (def_val == nullptr) {
                def_val = meta_obj->get_child("deferred_events");
            }
            if (def_val == nullptr) {
                def_val = meta_obj->get_child("defer");
            }
        }
        if (def_val != nullptr && curr != nullptr) {
            if (def_val->is_string()) {
                curr->deferred_events.push_back(sanitize_identifier(def_val->str_val));
            } else if (def_val->is_array()) {
                for (const auto& d_item : def_val->arr_val) {
                    if (d_item.is_string()) {
                        curr->deferred_events.push_back(sanitize_identifier(d_item.str_val));
                    }
                }
            }
        }

        // Parse nested states
        const auto* sub_states = state_data.get_child("states");
        if (sub_states != nullptr && sub_states->is_object()) {
            if (curr != nullptr) {
                curr->is_composite = true;
            }
            parse_states_object(*sub_states, model, state_name);
        }

        // Parse transitions
        const auto* on_obj = state_data.get_child("on");
        if (on_obj != nullptr && on_obj->is_object()) {
            parse_on_transitions(*on_obj, state_name, model);
        }

        // Parse after (XState v4/v5 delayed transitions: string, object with guard/actions, or array)
        const auto* after_obj = state_data.get_child("after");
        if (after_obj != nullptr && after_obj->is_object()) {
            auto make_after_tt = [](const std::string& delay_str) -> JsonValue {
                JsonValue tt;
                tt.type = JsonType::Object;
                JsonValue k;
                k.type = JsonType::String;
                k.str_val = "after";
                tt.set_member("kind", std::move(k));
                JsonValue dur;
                dur.type = JsonType::Number;
                dur.num_val = static_cast<double>(std::strtoull(delay_str.c_str(), nullptr, 10));
                tt.set_member("duration_value", dur);
                tt.set_member("duration_ms", dur);
                JsonValue u;
                u.type = JsonType::String;
                u.str_val = "ms";
                tt.set_member("unit", std::move(u));
                return tt;
            };
            auto parse_after_entry = [&](const JsonValue& entry, const std::string& delay_str) {
                JsonValue enriched;
                if (entry.is_object()) {
                    enriched = entry;
                } else if (entry.is_string()) {
                    enriched.type = JsonType::Object;
                    enriched.set_member("target", entry);
                } else {
                    return;
                }
                if (enriched.get_child("time_trigger") == nullptr) {
                    enriched.set_member("time_trigger", make_after_tt(delay_str));
                }
                parse_single_transition_object(enriched, state_name, "", model);
            };

            for (std::size_t a_idx = 0; a_idx < after_obj->obj_keys.size(); ++a_idx) {
                const auto& delay_str = after_obj->obj_keys[a_idx];
                const auto& target_val = after_obj->obj_values[a_idx];
                if (target_val.is_array()) {
                    for (const auto& elem : target_val.arr_val) {
                        parse_after_entry(elem, delay_str);
                    }
                } else {
                    parse_after_entry(target_val, delay_str);
                }
            }
        }

        // Parse always (immediate transitions)
        const auto* always_val = state_data.get_child("always");
        if (always_val != nullptr) {
            if (always_val->is_object()) {
                parse_single_transition_object(*always_val, state_name, "always", model);
            } else if (always_val->is_string()) {
                JsonValue fake_obj;
                fake_obj.type = JsonType::Object;
                fake_obj.set_member("target", *always_val);
                parse_single_transition_object(fake_obj, state_name, "always", model);
            } else if (always_val->is_array()) {
                for (const auto& elem : always_val->arr_val) {
                    if (elem.is_object()) {
                        parse_single_transition_object(elem, state_name, "always", model);
                    } else if (elem.is_string()) {
                        JsonValue fake_obj;
                        fake_obj.type = JsonType::Object;
                        fake_obj.set_member("target", elem);
                        parse_single_transition_object(fake_obj, state_name, "always", model);
                    }
                }
            }
        }

        // If state was declared with XState "type": "parallel" without explicit orthogonal_regions,
        // register each direct child state as an orthogonal region.
        if (auto* par_node = model.find_state_mut(state_name);
            par_node != nullptr && par_node->kind == StateKind::Parallel && par_node->orthogonal_regions.empty()) {
            for (const auto& child : model.states) {
                if (child.parent_state == state_name) {
                    OrthogonalRegion reg;
                    reg.id = child.name;
                    reg.name = child.name;
                    reg.initial_state_id = child.initial_sub_state.empty() ? child.name : child.initial_sub_state;
                    par_node->orthogonal_regions.push_back(std::move(reg));
                }
            }
        }
    }
}

}  // namespace fsm::ir::json_detail

#include "fsm/ir/fsm_ir_deserializer.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/frontend/directive/guard_parser.hpp"

namespace fsm::ir {

namespace {

std::string sanitize_identifier(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            out.push_back(c);
        } else if (c == ' ' || c == '-') {
            out.push_back('_');
        }
    }
    return out;
}

enum class JsonType : std::uint8_t { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    std::vector<JsonValue> arr_val;
    std::map<std::string, JsonValue> obj_val;
    std::vector<std::pair<std::string, JsonValue>> obj_members;

    [[nodiscard]] bool is_string() const { return type == JsonType::String; }
    [[nodiscard]] bool is_object() const { return type == JsonType::Object; }
    [[nodiscard]] bool is_array() const { return type == JsonType::Array; }
    [[nodiscard]] bool is_number() const { return type == JsonType::Number; }

    [[nodiscard]] std::string get_string(const std::string& key, const std::string& default_val = "") const {
        if (!is_object())
            return default_val;
        auto it = obj_val.find(key);
        return (it != obj_val.end() && it->second.is_string()) ? it->second.str_val : default_val;
    }

    [[nodiscard]] double get_number(const std::string& key, double default_val = 0.0) const {
        if (!is_object())
            return default_val;
        auto it = obj_val.find(key);
        return (it != obj_val.end() && it->second.type == JsonType::Number) ? it->second.num_val : default_val;
    }

    [[nodiscard]] const JsonValue* get_child(const std::string& key) const {
        if (!is_object())
            return nullptr;
        auto it = obj_val.find(key);
        return (it != obj_val.end()) ? &it->second : nullptr;
    }
};

class SimpleJsonParser {
  public:
    static bool parse(std::string_view text, JsonValue& out_val, std::string& err) {
        size_t idx = 0;
        skip_ws(text, idx);
        if (idx >= text.size()) {
            err = "Empty JSON input";
            return false;
        }
        return parse_value(text, idx, out_val, err);
    }

  private:
    static void skip_ws(std::string_view text, size_t& idx) {
        while (idx < text.size() && (std::isspace(static_cast<unsigned char>(text[idx])) != 0)) {
            ++idx;
        }
    }

    static bool parse_value(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err) {
        skip_ws(text, idx);
        if (idx >= text.size()) {
            err = "Unexpected end of input while expecting value";
            return false;
        }
        char c = text[idx];
        if (c == '{')
            return parse_object(text, idx, out_val, err);
        if (c == '[')
            return parse_array(text, idx, out_val, err);
        if (c == '"')
            return parse_string(text, idx, out_val, err);
        if (c == 't' || c == 'f')
            return parse_bool(text, idx, out_val, err);
        if (c == 'n')
            return parse_null(text, idx, out_val, err);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
            return parse_number(text, idx, out_val, err);
        err = std::string("Unexpected character: '") + c + "'";
        return false;
    }

    static bool parse_object(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err) {
        ++idx;  // skip '{'
        out_val.type = JsonType::Object;
        out_val.obj_val.clear();
        out_val.obj_members.clear();
        while (idx < text.size()) {
            skip_ws(text, idx);
            if (idx >= text.size()) {
                err = "Unterminated object: expected '}'";
                return false;
            }
            if (text[idx] == '}') {
                ++idx;
                return true;
            }
            if (text[idx] != '"') {
                err = "Expected string key in object";
                return false;
            }
            JsonValue key_val;
            if (!parse_string(text, idx, key_val, err))
                return false;
            skip_ws(text, idx);
            if (idx >= text.size() || text[idx] != ':') {
                err = "Expected ':' after key";
                return false;
            }
            ++idx;
            JsonValue member_val;
            if (!parse_value(text, idx, member_val, err))
                return false;
            out_val.obj_val[key_val.str_val] = member_val;
            out_val.obj_members.emplace_back(key_val.str_val, std::move(member_val));
            skip_ws(text, idx);
            if (idx < text.size() && text[idx] == ',') {
                ++idx;
            } else if (idx < text.size() && text[idx] == '}') {
                ++idx;
                return true;
            } else {
                err = "Expected ',' or '}' in object";
                return false;
            }
        }
        err = "Unterminated object";
        return false;
    }

    static bool parse_array(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err) {
        ++idx;  // skip '['
        out_val.type = JsonType::Array;
        out_val.arr_val.clear();
        while (idx < text.size()) {
            skip_ws(text, idx);
            if (idx >= text.size()) {
                err = "Unterminated array: expected ']'";
                return false;
            }
            if (text[idx] == ']') {
                ++idx;
                return true;
            }
            JsonValue elem;
            if (!parse_value(text, idx, elem, err))
                return false;
            out_val.arr_val.push_back(std::move(elem));
            skip_ws(text, idx);
            if (idx < text.size() && text[idx] == ',') {
                ++idx;
            } else if (idx < text.size() && text[idx] == ']') {
                ++idx;
                return true;
            } else {
                err = "Expected ',' or ']' in array";
                return false;
            }
        }
        err = "Unterminated array";
        return false;
    }

    static bool parse_string(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err) {
        ++idx;  // skip opening '"'
        out_val.type = JsonType::String;
        out_val.str_val.clear();
        while (idx < text.size()) {
            char c = text[idx++];
            if (c == '"')
                return true;
            if (c == '\\') {
                if (idx >= text.size()) {
                    err = "Unterminated escape sequence";
                    return false;
                }
                char esc = text[idx++];
                switch (esc) {
                    case '"':
                        out_val.str_val += '"';
                        break;
                    case '\\':
                        out_val.str_val += '\\';
                        break;
                    case '/':
                        out_val.str_val += '/';
                        break;
                    case 'b':
                        out_val.str_val += '\b';
                        break;
                    case 'f':
                        out_val.str_val += '\f';
                        break;
                    case 'n':
                        out_val.str_val += '\n';
                        break;
                    case 'r':
                        out_val.str_val += '\r';
                        break;
                    case 't':
                        out_val.str_val += '\t';
                        break;
                    case 'u': {
                        if (idx + 4 > text.size()) {
                            err = "Incomplete unicode escape";
                            return false;
                        }
                        idx += 4;
                        out_val.str_val += '?';
                        break;
                    }
                    default:
                        out_val.str_val += esc;
                        break;
                }
            } else {
                out_val.str_val += c;
            }
        }
        err = "Unterminated string";
        return false;
    }

    static bool parse_bool(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err) {
        if (text.substr(idx, 4) == "true") {
            out_val.type = JsonType::Bool;
            out_val.bool_val = true;
            idx += 4;
            return true;
        }
        if (text.substr(idx, 5) == "false") {
            out_val.type = JsonType::Bool;
            out_val.bool_val = false;
            idx += 5;
            return true;
        }
        err = "Invalid boolean token";
        return false;
    }

    static bool parse_null(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err) {
        if (text.substr(idx, 4) == "null") {
            out_val.type = JsonType::Null;
            idx += 4;
            return true;
        }
        err = "Invalid null token";
        return false;
    }

    static bool parse_number(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err) {
        size_t start = idx;
        if (text[idx] == '-')
            ++idx;
        while (idx < text.size() && std::isdigit(static_cast<unsigned char>(text[idx])))
            ++idx;
        if (idx < text.size() && text[idx] == '.') {
            ++idx;
            while (idx < text.size() && std::isdigit(static_cast<unsigned char>(text[idx])))
                ++idx;
        }
        if (idx < text.size() && (text[idx] == 'e' || text[idx] == 'E')) {
            ++idx;
            if (idx < text.size() && (text[idx] == '+' || text[idx] == '-'))
                ++idx;
            while (idx < text.size() && std::isdigit(static_cast<unsigned char>(text[idx])))
                ++idx;
        }
        std::string num_str(text.substr(start, idx - start));
        try {
            out_val.type = JsonType::Number;
            out_val.num_val = std::stod(num_str);
            return true;
        } catch (...) {
            err = "Invalid number format: " + num_str;
            return false;
        }
    }
};

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

    TransitionEdge trans;
    trans.source = source_state;
    trans.target = sanitize_identifier(clean_target);
    trans.target_is_history = is_history;
    trans.target_is_deep_history = is_deep;

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
    if (!guard_str.empty()) {
        auto parsed = frontend::directive::GuardExpressionParser::parse(guard_str);
        if (!parsed.cpp_type.empty()) {
            trans.guard = parsed.cpp_type;
            for (const auto& atomic : parsed.atomic_guards) {
                model.add_guard(atomic);
            }
        } else {
            trans.guard = guard_str;
            model.add_guard(guard_str);
        }
    }

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
        std::string act_san = sanitize_identifier(act_str);
        model.add_action(act_san);
        trans.transition_action = ActionSignature{act_san};
    }

    if (trans_obj.get_child("priority") != nullptr) {
        trans.priority = static_cast<int>(trans_obj.get_number("priority", 0.0));
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

    // Time trigger
    if (const auto* tt_val = trans_obj.get_child("time_trigger")) {
        if (tt_val->is_object()) {
            TimeTrigger tt;
            std::string k = tt_val->get_string("kind", "after");
            tt.kind = (k == "every") ? TimeTriggerKind::Every : TimeTriggerKind::After;
            tt.duration_value =
                static_cast<uint64_t>(tt_val->get_number("duration_value", tt_val->get_number("duration_ms", 0.0)));
            std::string u = tt_val->get_string("unit", "ms");
            tt.unit = (u == "s") ? TimeUnit::Seconds : ((u == "us") ? TimeUnit::Microseconds : TimeUnit::Milliseconds);
            tt.dynamic_expression = tt_val->get_string("dynamic_expression");
            trans.trigger = tt;
        }
    }

    // Parse assignments
    if (const auto* assigns_val = trans_obj.get_child("assignments")) {
        if (assigns_val->is_array()) {
            std::string act_name = trans.get_action().empty() ? "trans_action" : trans.get_action();
            ActionSignature act_sig(act_name);
            for (const auto& a_val : assigns_val->arr_val) {
                if (a_val.is_object()) {
                    ActionAssignment assign;
                    assign.target.name = a_val.get_string("variable");
                    assign.target.scope = LValueScope::Local;
                    std::string op_str = a_val.get_string("op", "=");
                    assign.op = string_to_assignment_op(op_str);
                    assign.expression = a_val.get_string("expression");
                    act_sig.assignments.push_back(std::move(assign));
                }
            }
            if (!act_sig.assignments.empty()) {
                trans.transition_action = std::move(act_sig);
            }
        }
    }

    const auto* src_node = model.find_state(source_state);
    trans.parent_scope = (src_node != nullptr) ? src_node->parent_state : "";
    if (trans.target.empty()) {
        trans.target = source_state;
        trans.kind = TransitionEdgeKind::Internal;
    }
    if (!model.is_choice_node(trans.target)) {
        model.add_state(trans.target);
    }
    if (!trans.event.empty()) {
        model.add_event(trans.event);
    }
    model.add_transition(std::move(trans));
}

void parse_on_transitions(const JsonValue& on_obj, const std::string& source_state, FsmIr& model) {
    for (const auto& [evt_key, trans_val] : on_obj.obj_members) {
        if (trans_val.is_object()) {
            parse_single_transition_object(trans_val, source_state, evt_key, model);
        } else if (trans_val.is_string()) {
            JsonValue fake_obj;
            fake_obj.type = JsonType::Object;
            fake_obj.obj_val["target"] = trans_val;
            parse_single_transition_object(fake_obj, source_state, evt_key, model);
        } else if (trans_val.is_array()) {
            for (const auto& elem : trans_val.arr_val) {
                if (elem.is_object()) {
                    parse_single_transition_object(elem, source_state, evt_key, model);
                } else if (elem.is_string()) {
                    JsonValue fake_obj;
                    fake_obj.type = JsonType::Object;
                    fake_obj.obj_val["target"] = elem;
                    parse_single_transition_object(fake_obj, source_state, evt_key, model);
                }
            }
        }
    }
}

void parse_states_object(const JsonValue& states_obj, FsmIr& model, const std::string& current_parent) {
    for (const auto& [state_key, state_data] : states_obj.obj_members) {
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
            continue;
        }

        auto* curr = model.find_state_mut(state_name);

        // Parse entry actions
        const auto* entry_val = state_data.get_child("entry");
        if (entry_val != nullptr && curr != nullptr) {
            if (entry_val->is_string()) {
                std::string act = sanitize_identifier(entry_val->str_val);
                model.add_action(act);
                curr->entry_actions.push_back(ActionSignature{act});
            } else if (entry_val->is_array()) {
                for (const auto& item : entry_val->arr_val) {
                    if (item.is_string()) {
                        std::string act = sanitize_identifier(item.str_val);
                        model.add_action(act);
                        curr->entry_actions.push_back(ActionSignature{act});
                    }
                }
            }
        }

        // Parse exit actions
        const auto* exit_val = state_data.get_child("exit");
        if (exit_val != nullptr && curr != nullptr) {
            if (exit_val->is_string()) {
                std::string act = sanitize_identifier(exit_val->str_val);
                model.add_action(act);
                curr->exit_actions.push_back(ActionSignature{act});
            } else if (exit_val->is_array()) {
                for (const auto& item : exit_val->arr_val) {
                    if (item.is_string()) {
                        std::string act = sanitize_identifier(item.str_val);
                        model.add_action(act);
                        curr->exit_actions.push_back(ActionSignature{act});
                    }
                }
            }
        }

        // Parse type / history
        std::string s_type = state_data.get_string("type");
        if (s_type == "history" && curr != nullptr) {
            curr->has_history = true;
            std::string h_mode = state_data.get_string("history", "shallow");
            curr->has_deep_history = (h_mode == "deep");
        } else if (s_type == "compound" && curr != nullptr) {
            curr->is_composite = true;
            std::string sub_init = state_data.get_string("initial");
            if (!sub_init.empty()) {
                curr->initial_sub_state = sanitize_identifier(sub_init);
            }
        }

        std::string s_kind = state_data.get_string("kind");
        if (!s_kind.empty() && curr != nullptr) {
            curr->kind = state_kind_from_string(s_kind);
        }
        std::string s_ti = state_data.get_string("time_invariant");
        if (!s_ti.empty() && curr != nullptr) {
            curr->time_invariant = StateTimeInvariant(s_ti);
        }

        // Parse do activity
        std::string s_do = state_data.get_string("do");
        if (s_do.empty()) {
            s_do = state_data.get_string("do_activity");
        }
        if (!s_do.empty() && curr != nullptr) {
            curr->do_activity = s_do;
        }

        // Parse satisfies / requirements
        const auto* reqs_val = state_data.get_child("satisfies");
        if (reqs_val == nullptr) {
            reqs_val = state_data.get_child("requirements");
        }
        if (reqs_val == nullptr) {
            reqs_val = state_data.get_child("traceability_reqs");
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

        // Parse deferred events
        const auto* def_val = state_data.get_child("defer");
        if (def_val == nullptr) {
            def_val = state_data.get_child("deferred_events");
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

        // Parse after (time triggers)
        const auto* after_obj = state_data.get_child("after");
        if (after_obj != nullptr && after_obj->is_object()) {
            for (const auto& [delay_str, target_val] : after_obj->obj_members) {
                TransitionEdge t;
                t.source = state_name;
                if (target_val.is_string()) {
                    t.target = sanitize_identifier(target_val.str_val);
                } else if (target_val.is_object()) {
                    t.target = sanitize_identifier(target_val.get_string("target"));
                }
                TimeTrigger tt;
                tt.kind = TimeTriggerKind::After;
                tt.duration_value = static_cast<uint64_t>(std::strtoull(delay_str.c_str(), nullptr, 10));
                tt.unit = TimeUnit::Milliseconds;
                t.trigger = tt;
                model.add_transition(std::move(t));
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
                fake_obj.obj_val["target"] = *always_val;
                parse_single_transition_object(fake_obj, state_name, "always", model);
            } else if (always_val->is_array()) {
                for (const auto& elem : always_val->arr_val) {
                    if (elem.is_object()) {
                        parse_single_transition_object(elem, state_name, "always", model);
                    } else if (elem.is_string()) {
                        JsonValue fake_obj;
                        fake_obj.type = JsonType::Object;
                        fake_obj.obj_val["target"] = elem;
                        parse_single_transition_object(fake_obj, state_name, "always", model);
                    }
                }
            }
        }
    }
}

}  // namespace

// ============================================================================
// FsmIrDeserializer Public API Implementation
// ============================================================================

bool FsmIrDeserializer::deserialize_json(std::string_view json_content, FsmIr& out_ir, std::string& out_error) {
    JsonValue root;
    if (!SimpleJsonParser::parse(json_content, root, out_error)) {
        out_error = "JSON Deserializer: Failed to parse JSON: " + out_error;
        return false;
    }

    if (!root.is_object()) {
        out_error = "JSON Deserializer: Root JSON must be an object.";
        return false;
    }

    std::string fsm_id = root.get_string("id");
    if (!fsm_id.empty()) {
        out_ir.name = sanitize_identifier(fsm_id);
    }
    std::string sm_name = root.get_string("name");
    if (!sm_name.empty()) {
        out_ir.name = sanitize_identifier(sm_name);
    }

    std::string init_state = root.get_string("initial");
    if (!init_state.empty()) {
        out_ir.initial_state = sanitize_identifier(init_state);
    }

    std::string pkg = root.get_string("package");
    if (!pkg.empty()) {
        out_ir.package = pkg;
    }

    const auto* attrs_obj = root.get_child("attributes");
    if (attrs_obj != nullptr && attrs_obj->is_object()) {
        for (const auto& [k, v] : attrs_obj->obj_val) {
            if (v.is_string()) {
                out_ir.attributes[k] = v.str_val;
            }
        }
    }

    // Variables
    const auto* vars_arr = root.get_child("variables");
    if (vars_arr != nullptr && vars_arr->is_array()) {
        for (const auto& v_val : vars_arr->arr_val) {
            if (v_val.is_object()) {
                VariableDefinition var;
                var.name = sanitize_identifier(v_val.get_string("name"));
                var.type = v_val.get_string("type", "uint32_t");
                var.initial_value = v_val.get_string("init");
                var.description = v_val.get_string("description");
                if (!var.name.empty()) {
                    out_ir.add_variable(std::move(var));
                }
            }
        }
    }

    // Ports
    const auto* ports_arr = root.get_child("ports");
    if (ports_arr != nullptr && ports_arr->is_array()) {
        for (const auto& p_val : ports_arr->arr_val) {
            if (p_val.is_object()) {
                PortDefinition port;
                port.name = sanitize_identifier(p_val.get_string("name"));
                port.type = p_val.get_string("type", "float");
                std::string dir_str = p_val.get_string("direction", "in");
                port.direction = string_to_port_direction(dir_str);
                if (const auto* min_c = p_val.get_child("min")) {
                    if (min_c->type == JsonType::Number)
                        port.min_value = min_c->num_val;
                }
                if (const auto* max_c = p_val.get_child("max")) {
                    if (max_c->type == JsonType::Number)
                        port.max_value = max_c->num_val;
                }
                port.constraint = p_val.get_string("constraint");
                if (!port.name.empty()) {
                    out_ir.ports.push_back(std::move(port));
                }
            }
        }
    }

    // Signals
    const auto* sigs_arr = root.get_child("signals");
    if (sigs_arr != nullptr && sigs_arr->is_array()) {
        for (const auto& s_val : sigs_arr->arr_val) {
            if (s_val.is_object()) {
                SignalDefinition sig;
                sig.name = sanitize_identifier(s_val.get_string("name"));
                const auto* attrs_arr = s_val.get_child("attributes");
                if (attrs_arr != nullptr && attrs_arr->is_array()) {
                    for (const auto& a_val : attrs_arr->arr_val) {
                        if (a_val.is_object()) {
                            SignalAttribute attr;
                            attr.name = sanitize_identifier(a_val.get_string("name"));
                            attr.type = a_val.get_string("type", "uint32_t");
                            attr.default_value = a_val.get_string("default");
                            sig.attributes.push_back(std::move(attr));
                        }
                    }
                }
                if (!sig.name.empty()) {
                    out_ir.signals.push_back(std::move(sig));
                }
            }
        }
    }

    // Enums
    const auto* enums_arr = root.get_child("enums");
    if (enums_arr != nullptr && enums_arr->is_array()) {
        for (const auto& e_val : enums_arr->arr_val) {
            if (e_val.is_object()) {
                std::string e_name = sanitize_identifier(e_val.get_string("name"));
                std::string e_type = e_val.get_string("underlying_type", e_val.get_string("type", "uint32_t"));
                EnumDefinition en(e_name, e_type);
                en.description = e_val.get_string("description");
                const auto* lits_arr = e_val.get_child("literals");
                if (lits_arr != nullptr && lits_arr->is_array()) {
                    for (const auto& l_val : lits_arr->arr_val) {
                        if (l_val.is_object()) {
                            std::string l_name = sanitize_identifier(l_val.get_string("name"));
                            std::optional<int64_t> l_num;
                            if (const auto* v_num = l_val.get_child("value")) {
                                if (v_num->type == JsonType::Number) {
                                    l_num = static_cast<int64_t>(v_num->num_val);
                                }
                            }
                            if (!l_name.empty()) {
                                EnumLiteral el(l_name, l_num);
                                el.description = l_val.get_string("description");
                                en.add_literal(std::move(el));
                            }
                        }
                    }
                }
                if (!en.name.empty()) {
                    out_ir.add_enum(std::move(en));
                }
            }
        }
    }

    // Structs
    const auto* structs_arr = root.get_child("structs");
    if (structs_arr != nullptr && structs_arr->is_array()) {
        for (const auto& s_val : structs_arr->arr_val) {
            if (s_val.is_object()) {
                std::string st_name = sanitize_identifier(s_val.get_string("name"));
                StructDefinition st(st_name);
                st.description = s_val.get_string("description");
                if (const auto* dt_val = s_val.get_child("is_datatype")) {
                    st.is_datatype = (dt_val->type == JsonType::Bool && dt_val->bool_val);
                }
                const auto* fields_arr = s_val.get_child("fields");
                if (fields_arr != nullptr && fields_arr->is_array()) {
                    for (const auto& f_val : fields_arr->arr_val) {
                        if (f_val.is_object()) {
                            std::string f_name = sanitize_identifier(f_val.get_string("name"));
                            std::string f_type = f_val.get_string("type", "string");
                            std::string f_def = f_val.get_string("default_value", f_val.get_string("default"));
                            std::string f_unit = f_val.get_string("physical_unit");
                            std::optional<double> f_min;
                            if (const auto* m_val = f_val.get_child("min_value")) {
                                if (m_val->type == JsonType::Number)
                                    f_min = m_val->num_val;
                            }
                            std::optional<double> f_max;
                            if (const auto* m_val = f_val.get_child("max_value")) {
                                if (m_val->type == JsonType::Number)
                                    f_max = m_val->num_val;
                            }
                            std::string f_desc = f_val.get_string("description");
                            if (!f_name.empty()) {
                                StructField sf(f_name, f_type, f_def);
                                if (!f_unit.empty())
                                    sf.physical_unit = f_unit;
                                sf.min_value = f_min;
                                sf.max_value = f_max;
                                sf.description = f_desc;
                                st.add_field(std::move(sf));
                            }
                        }
                    }
                }
                if (!st.name.empty()) {
                    out_ir.add_struct(std::move(st));
                }
            }
        }
    }

    // Types
    const auto* types_arr = root.get_child("types");
    if (types_arr != nullptr && types_arr->is_array()) {
        for (const auto& t_val : types_arr->arr_val) {
            if (t_val.is_object()) {
                TypeDefinition td;
                td.name = sanitize_identifier(t_val.get_string("name"));
                std::string kind_str = t_val.get_string("kind", "struct");
                td.kind = string_to_type_kind(kind_str);
                td.underlying_type = t_val.get_string("underlying_type");
                td.description = t_val.get_string("description");
                if (const auto* dt_val = t_val.get_child("is_datatype")) {
                    td.is_datatype = (dt_val->type == JsonType::Bool && dt_val->bool_val);
                }
                const auto* lits_arr = t_val.get_child("literals");
                if (lits_arr != nullptr && lits_arr->is_array()) {
                    for (const auto& l_val : lits_arr->arr_val) {
                        if (l_val.is_object()) {
                            std::string l_name = sanitize_identifier(l_val.get_string("name"));
                            std::optional<int64_t> l_num;
                            if (const auto* v_num = l_val.get_child("value")) {
                                if (v_num->type == JsonType::Number) {
                                    l_num = static_cast<int64_t>(v_num->num_val);
                                }
                            }
                            std::string l_desc = l_val.get_string("description");
                            if (!l_name.empty()) {
                                td.add_literal(l_name, l_num, l_desc);
                            }
                        }
                    }
                }
                const auto* fields_arr = t_val.get_child("fields");
                if (fields_arr != nullptr && fields_arr->is_array()) {
                    for (const auto& f_val : fields_arr->arr_val) {
                        if (f_val.is_object()) {
                            std::string f_name = sanitize_identifier(f_val.get_string("name"));
                            std::string f_type = f_val.get_string("type", "string");
                            std::string f_def = f_val.get_string("default_value", f_val.get_string("default"));
                            std::string f_desc = f_val.get_string("description");
                            if (!f_name.empty()) {
                                td.add_field(StructField(f_name, f_type, f_def, std::nullopt, std::nullopt,
                                                         std::nullopt, f_desc));
                            }
                        }
                    }
                }
                if (!td.name.empty()) {
                    out_ir.add_type(std::move(td));
                }
            }
        }
    }

    // Properties
    const auto* props_arr = root.get_child("properties");
    if (props_arr != nullptr && props_arr->is_array()) {
        for (const auto& p_val : props_arr->arr_val) {
            if (p_val.is_object()) {
                std::string p_name = p_val.get_string("name");
                std::string formula = p_val.get_string("ltl", p_val.get_string("formula"));
                std::string kind_str = p_val.get_string("kind", "Safety");
                PropertyKind p_kind = PropertyKind::Safety;
                if (kind_str == "Invariant")
                    p_kind = PropertyKind::Invariant;
                else if (kind_str == "Reachability")
                    p_kind = PropertyKind::Reachability;
                else if (kind_str == "Liveness")
                    p_kind = PropertyKind::Liveness;
                std::string req = p_val.get_string("req", p_val.get_string("traceability_req"));
                std::string desc = p_val.get_string("desc", p_val.get_string("description"));
                FormalProperty prop(p_name, p_kind, formula, desc, req);
                out_ir.properties.push_back(std::move(prop));
            }
        }
    }

    // States
    const auto* states_obj = root.get_child("states");
    if (states_obj != nullptr && states_obj->is_object()) {
        parse_states_object(*states_obj, out_ir, "");
    } else if (states_obj != nullptr && states_obj->is_array()) {
        for (const auto& s_item : states_obj->arr_val) {
            if (s_item.is_object()) {
                std::string s_name = sanitize_identifier(s_item.get_string("name", s_item.get_string("id")));
                std::string p_name = sanitize_identifier(s_item.get_string("parent"));
                if (!s_name.empty()) {
                    out_ir.add_state(s_name, p_name);
                }
            }
        }
    } else {
        out_error = "JSON Deserializer: Missing or invalid 'states' in JSON.";
        return false;
    }

    // Top-level Transitions list if present (only if transitions were not already parsed from states.on)
    if (out_ir.transitions.empty()) {
        const auto* trans_arr = root.get_child("transitions");
        if (trans_arr != nullptr && trans_arr->is_array()) {
            for (const auto& t_val : trans_arr->arr_val) {
                if (t_val.is_object()) {
                    std::string src = sanitize_identifier(t_val.get_string("source"));
                    std::string evt = t_val.get_string("event");
                    if (evt.empty()) {
                        evt = t_val.get_string("trigger");
                    }
                    parse_single_transition_object(t_val, src, evt, out_ir);
                }
            }
        }
    }

    if (out_ir.states.empty()) {
        out_error = "JSON Deserializer: No states extracted from JSON.";
        return false;
    }

    if (out_ir.initial_state.empty() && !out_ir.states.empty()) {
        out_ir.initial_state = out_ir.states.front().name;
    }

    return true;
}

}  // namespace fsm::ir

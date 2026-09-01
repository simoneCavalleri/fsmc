#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

// ============================================================================
// Lightweight Zero-Dependency JSON Value Representation
// ============================================================================
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
        if (!is_object()) {
            return default_val;
        }
        auto find_it = obj_val.find(key);
        if (find_it != obj_val.end() && find_it->second.is_string()) {
            return find_it->second.str_val;
        }
        return default_val;
    }

    [[nodiscard]] double get_number(const std::string& key, double default_val = 0.0) const {
        if (!is_object()) {
            return default_val;
        }
        auto find_it = obj_val.find(key);
        if (find_it != obj_val.end() && find_it->second.type == JsonType::Number) {
            return find_it->second.num_val;
        }
        return default_val;
    }

    [[nodiscard]] const JsonValue* get_child(const std::string& key) const {
        if (!is_object()) {
            return nullptr;
        }
        auto find_it = obj_val.find(key);
        return (find_it != obj_val.end()) ? &find_it->second : nullptr;
    }
};

// ============================================================================
// Lightweight JSON Parser
// ============================================================================
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
            idx++;
        }
    }

    static bool parse_value(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err) {
        skip_ws(text, idx);
        if (idx >= text.size()) {
            err = "Unexpected end of JSON";
            return false;
        }

        char ch = text[idx];
        if (ch == '{') {
            return parse_object(text, idx, out_val, err);
        }
        if (ch == '[') {
            return parse_array(text, idx, out_val, err);
        }
        if (ch == '"') {
            return parse_string(text, idx, out_val, err);
        }
        if (ch == 't' || ch == 'f') {
            return parse_bool(text, idx, out_val, err);
        }
        if (ch == 'n') {
            return parse_null(text, idx, out_val, err);
        }
        if (ch == '-' || (std::isdigit(static_cast<unsigned char>(ch)) != 0)) {
            return parse_number(text, idx, out_val, err);
        }

        err = std::string("Unexpected character in JSON: '") + ch + "'";
        return false;
    }

    static bool parse_object(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err) {
        out_val.type = JsonType::Object;
        idx++;  // skip '{'
        skip_ws(text, idx);

        if (idx < text.size() && text[idx] == '}') {
            idx++;
            return true;
        }

        while (idx < text.size()) {
            skip_ws(text, idx);
            if (idx >= text.size() || text[idx] != '"') {
                err = "Expected string key in JSON object";
                return false;
            }

            JsonValue key_val;
            if (!parse_string(text, idx, key_val, err)) {
                return false;
            }

            skip_ws(text, idx);
            if (idx >= text.size() || text[idx] != ':') {
                err = "Expected ':' after key in JSON object";
                return false;
            }
            idx++;  // skip ':'

            JsonValue member_val;
            if (!parse_value(text, idx, member_val, err)) {
                return false;
            }

            out_val.obj_val[key_val.str_val] = member_val;
            out_val.obj_members.emplace_back(key_val.str_val, member_val);

            skip_ws(text, idx);
            if (idx < text.size() && text[idx] == ',') {
                idx++;
                continue;
            }
            if (idx < text.size() && text[idx] == '}') {
                idx++;
                return true;
            }
            err = "Expected ',' or '}' in JSON object";
            return false;
        }
        err = "Unclosed JSON object";
        return false;
    }

    static bool parse_array(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err) {
        out_val.type = JsonType::Array;
        idx++;  // skip '['
        skip_ws(text, idx);

        if (idx < text.size() && text[idx] == ']') {
            idx++;
            return true;
        }

        while (idx < text.size()) {
            JsonValue elem;
            if (!parse_value(text, idx, elem, err)) {
                return false;
            }
            out_val.arr_val.push_back(std::move(elem));

            skip_ws(text, idx);
            if (idx < text.size() && text[idx] == ',') {
                idx++;
                continue;
            }
            if (idx < text.size() && text[idx] == ']') {
                idx++;
                return true;
            }
            err = "Expected ',' or ']' in JSON array";
            return false;
        }
        err = "Unclosed JSON array";
        return false;
    }

    static bool parse_string(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err) {
        out_val.type = JsonType::String;
        idx++;  // skip opening '"'
        std::string res;

        while (idx < text.size()) {
            char ch = text[idx++];
            if (ch == '"') {
                out_val.str_val = res;
                return true;
            }
            if (ch == '\\') {
                if (idx >= text.size()) {
                    err = "Incomplete escape sequence in JSON string";
                    return false;
                }
                char esc = text[idx++];
                switch (esc) {
                    case '"':
                        res += '"';
                        break;
                    case '\\':
                        res += '\\';
                        break;
                    case '/':
                        res += '/';
                        break;
                    case 'b':
                        res += '\b';
                        break;
                    case 'f':
                        res += '\f';
                        break;
                    case 'n':
                        res += '\n';
                        break;
                    case 'r':
                        res += '\r';
                        break;
                    case 't':
                        res += '\t';
                        break;
                    case 'u':
                        if (idx + 4 <= text.size()) {
                            idx += 4;  // skip unicode hex sequence
                            res += '?';
                        }
                        break;
                    default:
                        res += esc;
                        break;
                }
            } else {
                res += ch;
            }
        }
        err = "Unterminated JSON string";
        return false;
    }

    static bool parse_bool(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err) {
        out_val.type = JsonType::Bool;
        if (text.substr(idx, 4) == "true") {
            out_val.bool_val = true;
            idx += 4;
            return true;
        }
        if (text.substr(idx, 5) == "false") {
            out_val.bool_val = false;
            idx += 5;
            return true;
        }
        err = "Invalid boolean in JSON";
        return false;
    }

    static bool parse_null(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err) {
        out_val.type = JsonType::Null;
        if (text.substr(idx, 4) == "null") {
            idx += 4;
            return true;
        }
        err = "Invalid null in JSON";
        return false;
    }

    static bool parse_number(std::string_view text, size_t& idx, JsonValue& out_val, std::string& /*err*/) {
        out_val.type = JsonType::Number;
        size_t start = idx;
        if (text[idx] == '-') {
            idx++;
        }
        while (idx < text.size() && ((std::isdigit(static_cast<unsigned char>(text[idx])) != 0) || text[idx] == '.' ||
                                     text[idx] == 'e' || text[idx] == 'E' || text[idx] == '+' || text[idx] == '-')) {
            idx++;
        }
        std::string num_str(text.substr(start, idx - start));
        out_val.num_val = std::stod(num_str);
        return true;
    }
};

// ============================================================================
// XState JSON Statechart Parser
// ============================================================================
class JsonStateParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Diagram; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "json"; }

    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override {
        JsonValue root;
        if (!SimpleJsonParser::parse(content, root, error_message)) {
            error_message = "JSON Parser: Failed to parse JSON: " + error_message;
            return false;
        }

        if (!root.is_object()) {
            error_message = "JSON Parser: Root JSON must be an object.";
            return false;
        }

        std::string fsm_id = root.get_string("id");
        if (!fsm_id.empty()) {
            model.name = sanitize_identifier(fsm_id);
        }

        std::string init_state = root.get_string("initial");
        if (!init_state.empty()) {
            model.initial_state = sanitize_identifier(init_state);
        }

        // Top-level variables / context
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
                        model.add_variable(std::move(var));
                    }
                }
            }
        }

        // Top-level ports
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
                        if (min_c->type == JsonType::Number) {
                            port.min_value = min_c->num_val;
                        }
                    }
                    if (const auto* max_c = p_val.get_child("max")) {
                        if (max_c->type == JsonType::Number) {
                            port.max_value = max_c->num_val;
                        }
                    }
                    port.constraint = p_val.get_string("constraint");
                    if (!port.name.empty()) {
                        model.ports.push_back(std::move(port));
                    }
                }
            }
        }

        // Top-level signals
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
                        model.signals.push_back(std::move(sig));
                        model.add_event(sig.name);
                    }
                }
            }
        }

        // Top-level formal properties
        const auto* props_arr = root.get_child("properties");
        if (props_arr != nullptr && props_arr->is_array()) {
            for (const auto& p_val : props_arr->arr_val) {
                if (p_val.is_object()) {
                    std::string p_name = p_val.get_string("name");
                    std::string formula = p_val.get_string("ltl");
                    if (formula.empty()) {
                        formula = p_val.get_string("formula");
                    }
                    std::string kind_str = p_val.get_string("kind", "Safety");
                    PropertyKind p_kind = PropertyKind::Safety;
                    if (kind_str == "Invariant") {
                        p_kind = PropertyKind::Invariant;
                    } else if (kind_str == "Reachability") {
                        p_kind = PropertyKind::Reachability;
                    } else if (kind_str == "Liveness") {
                        p_kind = PropertyKind::Liveness;
                    }
                    std::string req = p_val.get_string("req");
                    if (req.empty()) {
                        req = p_val.get_string("traceability_req");
                    }
                    std::string desc = p_val.get_string("desc");
                    if (desc.empty()) {
                        desc = p_val.get_string("description");
                    }
                    FormalProperty prop(p_name, p_kind, formula, desc, req);
                    model.properties.push_back(std::move(prop));
                }
            }
        }

        const auto* states_obj = root.get_child("states");
        if (states_obj != nullptr && states_obj->is_object()) {
            parse_states_object(*states_obj, model, "");
        } else {
            error_message = "JSON Parser: Missing or invalid 'states' object in JSON.";
            return false;
        }

        if (model.states.empty()) {
            error_message = "JSON Parser: No states extracted from JSON.";
            return false;
        }

        if (model.initial_state.empty() && !model.states.empty()) {
            model.initial_state = model.states.front().name;
        }

        return true;
    }

  private:
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

            // Parse do activity
            std::string do_act = state_data.get_string("do");
            if (!do_act.empty() && curr != nullptr) {
                curr->do_activity = sanitize_identifier(do_act);
            }

            // Parse kind (entryPoint, exitPoint, parallel, choice, fork, join)
            std::string kind_str = state_data.get_string("kind");
            if (kind_str.empty()) {
                kind_str = state_data.get_string("type");
            }
            if (!kind_str.empty() && curr != nullptr) {
                if (kind_str == "entryPoint" || kind_str == "entry_point" || kind_str == "EntryPoint") {
                    curr->kind = StateKind::EntryPoint;
                } else if (kind_str == "exitPoint" || kind_str == "exit_point" || kind_str == "ExitPoint") {
                    curr->kind = StateKind::ExitPoint;
                } else if (kind_str == "parallel" || kind_str == "Parallel") {
                    curr->kind = StateKind::Parallel;
                } else if (kind_str == "choice" || kind_str == "Choice") {
                    curr->kind = StateKind::Choice;
                } else if (kind_str == "fork" || kind_str == "Fork") {
                    curr->kind = StateKind::Fork;
                } else if (kind_str == "join" || kind_str == "Join") {
                    curr->kind = StateKind::Join;
                }
            }

            // Parse time_invariant
            std::string inv_str = state_data.get_string("time_invariant");
            if (inv_str.empty()) {
                inv_str = state_data.get_string("invariant");
            }
            if (!inv_str.empty() && curr != nullptr) {
                curr->time_invariant = inv_str;
            }

            // Parse traceability requirements
            const auto* reqs_val = state_data.get_child("satisfies");
            if (reqs_val == nullptr) {
                reqs_val = state_data.get_child("requirements");
            }
            if (reqs_val != nullptr && curr != nullptr) {
                if (reqs_val->is_string()) {
                    curr->traceability_reqs.push_back(reqs_val->str_val);
                } else if (reqs_val->is_array()) {
                    for (const auto& item : reqs_val->arr_val) {
                        if (item.is_string()) {
                            curr->traceability_reqs.push_back(item.str_val);
                        }
                    }
                }
            }

            // Check if composite state with nested "states"
            std::string sub_initial = state_data.get_string("initial");
            const auto* sub_states = state_data.get_child("states");
            if (sub_states != nullptr && sub_states->is_object()) {
                if (curr != nullptr) {
                    curr->is_composite = true;
                    if (!sub_initial.empty()) {
                        curr->initial_sub_state = sanitize_identifier(sub_initial);
                    }
                }
                parse_states_object(*sub_states, model, state_name);
            }

            // Check deferred events
            const auto* defer_val = state_data.get_child("defer");
            if (defer_val != nullptr && curr != nullptr) {
                if (defer_val->is_string()) {
                    curr->deferred_events.push_back(sanitize_identifier(defer_val->str_val));
                } else if (defer_val->is_array()) {
                    for (const auto& d_item : defer_val->arr_val) {
                        if (d_item.is_string()) {
                            curr->deferred_events.push_back(sanitize_identifier(d_item.str_val));
                        }
                    }
                }
            }

            // Check transitions on "on"
            const auto* on_obj = state_data.get_child("on");
            if (on_obj != nullptr && on_obj->is_object()) {
                parse_on_transitions(*on_obj, state_name, model);
            }
        }
    }

    static void parse_on_transitions(const JsonValue& on_obj, const std::string& source_state, FsmIr& model) {
        for (const auto& [event_key, trans_data] : on_obj.obj_members) {
            std::string event_name = sanitize_identifier(event_key);
            if (event_key == "EVENT" || event_key == "always" || event_key.empty() || event_key == "anonymous") {
                event_name = "Anonymous";
            }

            // Simple form: "EVENT": "TargetState"
            if (trans_data.is_string()) {
                TransitionEdge trans;
                trans.source = source_state;
                trans.target = sanitize_identifier(trans_data.str_val);
                trans.event = event_name;
                const auto* src_node = model.find_state(source_state);
                trans.parent_scope = (src_node != nullptr) ? src_node->parent_state : "";
                model.add_event(event_name);
                model.add_state(trans.target);
                model.add_transition(std::move(trans));
                continue;
            }

            // Object form: "EVENT": { "target": "...", "cond": "...", "actions": ["..."] }
            if (trans_data.is_object()) {
                parse_single_transition_object(trans_data, source_state, event_name, model);
            }

            // Array form: "EVENT": [ { ... }, { ... } ]
            if (trans_data.is_array()) {
                for (const auto& sub_trans : trans_data.arr_val) {
                    if (sub_trans.is_object()) {
                        parse_single_transition_object(sub_trans, source_state, event_name, model);
                    }
                }
            }
        }
    }

    static void parse_single_transition_object(const JsonValue& trans_obj, const std::string& source_state,
                                               const std::string& event_name, FsmIr& model) {
        std::string target = trans_obj.get_string("target");
        std::string cond = trans_obj.get_string("cond");
        if (cond.empty()) {
            cond = trans_obj.get_string("guard");
        }

        std::string action = trans_obj.get_string("action");
        if (action.empty()) {
            const auto* act_val = trans_obj.get_child("actions");
            if (act_val != nullptr) {
                if (act_val->is_string()) {
                    action = act_val->str_val;
                } else if (act_val->is_array() && !act_val->arr_val.empty() && act_val->arr_val.front().is_string()) {
                    action = act_val->arr_val.front().str_val;
                }
            }
        }

        std::uint32_t priority = 0;
        std::string prio_str = trans_obj.get_string("priority");
        if (!prio_str.empty()) {
            try {
                priority = static_cast<std::uint32_t>(std::stoul(prio_str));
            } catch (...) {
            }
        } else {
            const auto* p_val = trans_obj.get_child("priority");
            if (p_val != nullptr && p_val->type == JsonType::Number) {
                priority = static_cast<std::uint32_t>(p_val->num_val);
            }
        }

        bool is_internal = target.empty() || trans_obj.get_string("type") == "internal";
        std::string dst = target.empty() ? source_state : target;

        bool is_history = false;
        bool is_deep_history = false;
        const auto h_star_pos = dst.find("[H*]");
        const auto h_pos = dst.find("[H]");
        if (h_star_pos != std::string::npos) {
            is_history = true;
            is_deep_history = true;
            dst = dst.substr(0, h_star_pos);
        } else if (h_pos != std::string::npos) {
            is_history = true;
            is_deep_history = false;
            dst = dst.substr(0, h_pos);
        }

        std::string clean_target = sanitize_identifier(dst);

        TransitionEdge trans;
        trans.source = source_state;
        trans.target = clean_target;
        trans.target_is_history = is_history;
        trans.target_is_deep_history = is_deep_history;
        trans.event = event_name;
        trans.priority = priority;
        if (!cond.empty()) {
            auto parsed = GuardExpressionParser::parse(cond);
            if (!parsed.cpp_type.empty()) {
                trans.guard = parsed.cpp_type;
                for (const auto& atomic : parsed.atomic_guards) {
                    model.add_guard(atomic);
                }
            }
        }
        if (!action.empty()) {
            trans.action = sanitize_identifier(action);
            model.add_action(*trans.action);
        }
        const auto* src_node = model.find_state(source_state);
        trans.parent_scope = (src_node != nullptr) ? src_node->parent_state : "";
        trans.kind = is_internal ? TransitionEdgeKind::Internal : TransitionEdgeKind::External;

        if (!model.is_choice_node(trans.target)) {
            model.add_state(trans.target);
            if (is_history) {
                if (auto* target_state = model.find_state_mut(trans.target)) {
                    target_state->has_history = true;
                    if (is_deep_history) {
                        target_state->has_deep_history = true;
                    }
                }
            }
        }
        if (!trans.event.empty()) {
            model.add_event(trans.event);
        }

        model.add_transition(std::move(trans));
    }
};

}  // namespace fsm::codegen

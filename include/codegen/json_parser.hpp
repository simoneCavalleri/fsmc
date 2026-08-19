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

#include "codegen/fsm_model.hpp"
#include "codegen/parser_interface.hpp"

namespace fsm::codegen {

// ============================================================================
// Lightweight Zero-Dependency JSON Value Representation
// ============================================================================
enum class JsonType { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    std::vector<JsonValue> arr_val;
    std::map<std::string, JsonValue> obj_val;

    [[nodiscard]] bool is_string() const { return type == JsonType::String; }
    [[nodiscard]] bool is_object() const { return type == JsonType::Object; }
    [[nodiscard]] bool is_array() const { return type == JsonType::Array; }

    [[nodiscard]] std::string get_string(const std::string& key, const std::string& default_val = "") const {
        if (!is_object()) {
            return default_val;
        }
        auto it = obj_val.find(key);
        if (it != obj_val.end() && it->second.is_string()) {
            return it->second.str_val;
        }
        return default_val;
    }

    [[nodiscard]] const JsonValue* get_child(const std::string& key) const {
        if (!is_object()) {
            return nullptr;
        }
        auto it = obj_val.find(key);
        return (it != obj_val.end()) ? &it->second : nullptr;
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

            out_val.obj_val[key_val.str_val] = std::move(member_val);

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

    static bool parse_string(std::string_view text, size_t& idx, JsonValue& out_val, std::string& /*err*/) {
        out_val.type = JsonType::String;
        idx++;  // skip '"'
        std::string result;
        while (idx < text.size()) {
            char ch = text[idx++];
            if (ch == '"') {
                out_val.str_val = result;
                return true;
            }
            if (ch == '\\' && idx < text.size()) {
                char esc = text[idx++];
                if (esc == 'n') {
                    result += '\n';
                } else if (esc == 't') {
                    result += '\t';
                } else if (esc == 'r') {
                    result += '\r';
                } else if (esc == '"' || esc == '\\' || esc == '/') {
                    result += esc;
                } else {
                    result += esc;
                }
            } else {
                result += ch;
            }
        }
        out_val.str_val = result;
        return true;
    }

    static bool parse_bool(std::string_view text, size_t& idx, JsonValue& out_val, std::string& /*err*/) {
        out_val.type = JsonType::Bool;
        if (starts_with(text.substr(idx), "true")) {
            out_val.bool_val = true;
            idx += 4;
            return true;
        }
        if (starts_with(text.substr(idx), "false")) {
            out_val.bool_val = false;
            idx += 5;
            return true;
        }
        return false;
    }

    static bool parse_null(std::string_view text, size_t& idx, JsonValue& out_val, std::string& /*err*/) {
        out_val.type = JsonType::Null;
        if (starts_with(text.substr(idx), "null")) {
            idx += 4;
            return true;
        }
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
    [[nodiscard]] static std::string format_name() { return "json"; }

    bool parse(std::string_view content, FsmModel& model, std::string& error_message) override {
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
    void parse_states_object(const JsonValue& states_obj, FsmModel& model, const std::string& current_parent) {
        for (const auto& [state_key, state_data] : states_obj.obj_val) {
            const std::string state_name = sanitize_identifier(state_key);
            model.add_state(state_name, current_parent);

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

            // Check if composite state with nested "states"
            std::string sub_initial = state_data.get_string("initial");
            const auto* sub_states = state_data.get_child("states");
            if (sub_states != nullptr && sub_states->is_object()) {
                auto* curr = model.find_state_mut(state_name);
                if (curr != nullptr) {
                    curr->is_composite = true;
                    if (!sub_initial.empty()) {
                        curr->initial_sub_state = sanitize_identifier(sub_initial);
                    }
                }
                parse_states_object(*sub_states, model, state_name);
            }

            // Check transitions on "on"
            const auto* on_obj = state_data.get_child("on");
            if (on_obj != nullptr && on_obj->is_object()) {
                parse_on_transitions(*on_obj, state_name, model);
            }
        }
    }

    static void parse_on_transitions(const JsonValue& on_obj, const std::string& source_state, FsmModel& model) {
        for (const auto& [event_key, trans_data] : on_obj.obj_val) {
            std::string event_name = sanitize_identifier(event_key);

            // Simple form: "EVENT": "TargetState"
            if (trans_data.is_string()) {
                TransitionModel trans;
                trans.source = source_state;
                trans.target = sanitize_identifier(trans_data.str_val);
                trans.event = event_name;
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
                                               const std::string& event_name, FsmModel& model) {
        std::string target = trans_obj.get_string("target");
        std::string cond = trans_obj.get_string("cond");
        if (cond.empty()) {
            cond = trans_obj.get_string("guard");
        }

        std::string action;
        const auto* act_val = trans_obj.get_child("actions");
        if (act_val != nullptr) {
            if (act_val->is_string()) {
                action = act_val->str_val;
            } else if (act_val->is_array() && !act_val->arr_val.empty() && act_val->arr_val.front().is_string()) {
                action = act_val->arr_val.front().str_val;
            }
        }

        bool is_internal = target.empty() || trans_obj.get_string("type") == "internal";
        std::string dst = target.empty() ? source_state : target;

        TransitionModel trans;
        trans.source = source_state;
        trans.target = sanitize_identifier(dst);
        trans.event = event_name;
        if (!cond.empty()) {
            trans.guard = sanitize_identifier(cond);
            model.add_guard(*trans.guard);
        }
        if (!action.empty()) {
            trans.action = sanitize_identifier(action);
            model.add_action(*trans.action);
        }
        trans.kind = is_internal ? TransitionKind::Internal : TransitionKind::External;

        if (!model.is_choice_node(trans.target)) {
            model.add_state(trans.target);
        }
        if (!trans.event.empty()) {
            model.add_event(trans.event);
        }

        model.add_transition(std::move(trans));
    }
};

}  // namespace fsm::codegen

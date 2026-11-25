#include "fsm/ir/json_dom.hpp"

#include <cctype>
#include <cstdlib>

namespace fsm::ir {

void JsonValue::set_member(std::string key, JsonValue val) {
    if (auto it = obj_index.find(key); it != obj_index.end()) {
        obj_values[it->second] = std::move(val);
        return;
    }
    const std::size_t idx = obj_keys.size();
    obj_index.emplace(key, idx);
    obj_keys.push_back(std::move(key));
    obj_values.push_back(std::move(val));
}

std::string JsonValue::get_string(const std::string& key, const std::string& default_val) const {
    if (!is_object())
        return default_val;
    auto it = obj_index.find(key);
    return (it != obj_index.end() && obj_values[it->second].is_string()) ? obj_values[it->second].str_val : default_val;
}

double JsonValue::get_number(const std::string& key, double default_val) const {
    if (!is_object())
        return default_val;
    auto it = obj_index.find(key);
    return (it != obj_index.end() && obj_values[it->second].type == JsonType::Number) ? obj_values[it->second].num_val
                                                                                      : default_val;
}

const JsonValue* JsonValue::get_child(const std::string& key) const {
    if (!is_object())
        return nullptr;
    auto it = obj_index.find(key);
    return (it != obj_index.end()) ? &obj_values[it->second] : nullptr;
}

namespace {

class JsonParserImpl {
  public:
    static constexpr std::size_t kMaxJsonDepth = 128;

    static bool parse(std::string_view text, JsonValue& out_val, std::string& err) {
        size_t idx = 0;
        skip_ws(text, idx);
        if (idx >= text.size()) {
            err = "Empty JSON input";
            return false;
        }
        if (!parse_value(text, idx, out_val, err, 0)) {
            return false;
        }
        skip_ws(text, idx);
        if (idx < text.size()) {
            err = "Unexpected trailing characters after JSON root value";
            return false;
        }
        return true;
    }

  private:
    static void skip_ws(std::string_view text, size_t& idx) {
        while (idx < text.size() && (std::isspace(static_cast<unsigned char>(text[idx])) != 0)) {
            ++idx;
        }
    }

    static bool parse_value(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err,
                            std::size_t depth) {
        if (depth > kMaxJsonDepth) {
            err = "JSON nesting depth exceeded maximum limit (" + std::to_string(kMaxJsonDepth) + ")";
            return false;
        }
        skip_ws(text, idx);
        if (idx >= text.size()) {
            err = "Unexpected end of input while expecting value";
            return false;
        }
        char c = text[idx];
        if (c == '{')
            return parse_object(text, idx, out_val, err, depth + 1);
        if (c == '[')
            return parse_array(text, idx, out_val, err, depth + 1);
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

    static bool parse_object(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err,
                             std::size_t depth) {
        ++idx;  // skip '{'
        out_val.type = JsonType::Object;
        out_val.obj_keys.clear();
        out_val.obj_values.clear();
        out_val.obj_index.clear();
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
            if (!parse_value(text, idx, member_val, err, depth))
                return false;
            out_val.set_member(std::move(key_val.str_val), std::move(member_val));
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

    static bool parse_array(std::string_view text, size_t& idx, JsonValue& out_val, std::string& err,
                            std::size_t depth) {
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
            if (!parse_value(text, idx, elem, err, depth))
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
                        auto parse_hex4 = [&](size_t pos, uint32_t& out_cp) -> bool {
                            out_cp = 0;
                            for (size_t i = 0; i < 4; ++i) {
                                char hc = text[pos + i];
                                out_cp <<= 4u;
                                if (hc >= '0' && hc <= '9') {
                                    out_cp |= static_cast<uint32_t>(hc - '0');
                                } else if (hc >= 'a' && hc <= 'f') {
                                    out_cp |= static_cast<uint32_t>(hc - 'a' + 10);
                                } else if (hc >= 'A' && hc <= 'F') {
                                    out_cp |= static_cast<uint32_t>(hc - 'A' + 10);
                                } else {
                                    return false;
                                }
                            }
                            return true;
                        };
                        uint32_t cp = 0;
                        if (!parse_hex4(idx, cp)) {
                            err = "Invalid hex digits in unicode escape";
                            return false;
                        }
                        idx += 4;
                        if (cp >= 0xD800u && cp <= 0xDBFFu && idx + 6 <= text.size() && text[idx] == '\\' &&
                            text[idx + 1] == 'u') {
                            uint32_t low = 0;
                            if (parse_hex4(idx + 2, low) && low >= 0xDC00u && low <= 0xDFFFu) {
                                cp = 0x10000u + (((cp - 0xD800u) << 10u) | (low - 0xDC00u));
                                idx += 6;
                            }
                        }
                        if (cp <= 0x7Fu) {
                            out_val.str_val += static_cast<char>(cp);
                        } else if (cp <= 0x7FFu) {
                            out_val.str_val += static_cast<char>(0xC0u | ((cp >> 6u) & 0x1Fu));
                            out_val.str_val += static_cast<char>(0x80u | (cp & 0x3Fu));
                        } else if (cp <= 0xFFFFu) {
                            out_val.str_val += static_cast<char>(0xE0u | ((cp >> 12u) & 0x0Fu));
                            out_val.str_val += static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu));
                            out_val.str_val += static_cast<char>(0x80u | (cp & 0x3Fu));
                        } else if (cp <= 0x10FFFFu) {
                            out_val.str_val += static_cast<char>(0xF0u | ((cp >> 18u) & 0x07u));
                            out_val.str_val += static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu));
                            out_val.str_val += static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu));
                            out_val.str_val += static_cast<char>(0x80u | (cp & 0x3Fu));
                        }
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

}  // namespace

bool SimpleJsonParser::parse(std::string_view input, JsonValue& out_root, std::string& err) {
    return JsonParserImpl::parse(input, out_root, err);
}

}  // namespace fsm::ir

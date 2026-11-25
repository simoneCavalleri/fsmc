#include "cpp_emitter_utils.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

#include "fsm/ir/identifier_utils.hpp"

namespace fsm::backend::cpp::emitter_detail {

using namespace fsm::ir;

std::string resolve_external_cpp_symbol(std::string_view symbol) {
    if (symbol.empty()) {
        return {};
    }
    if (symbol.find("::") != std::string_view::npos) {
        return std::string(symbol);
    }
    std::string out = "::";
    out.reserve(symbol.size() + 4);
    for (char c : symbol) {
        if (c == '.') {
            out += "::";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string resolve_external_cpp_include(std::string_view module_path) {
    if (module_path.empty()) {
        return {};
    }
    if (module_path.front() == '<' || module_path.front() == '"') {
        return std::string(module_path);
    }
    const auto ends_with = [](std::string_view s, std::string_view suffix) {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    if (ends_with(module_path, ".hpp") || ends_with(module_path, ".h") || ends_with(module_path, ".hh") ||
        ends_with(module_path, ".hxx")) {
        return "\"" + std::string(module_path) + "\"";
    }
    return "\"" + std::string(module_path) + ".hpp\"";
}

bool is_ident_start(char c) noexcept {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_ident_char(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string escape_cpp_string_literal(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 4);
    for (unsigned char ch : text) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (ch < 0x20U) {
                    static constexpr char kHex[] = "0123456789abcdef";
                    out += "\\x";
                    out.push_back(kHex[(ch >> 4U) & 0xFU]);
                    out.push_back(kHex[ch & 0xFU]);
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

std::string sanitize_cpp_comment(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') {
            out += "* /";
            ++i;
        } else if (text[i] == '\r' || text[i] == '\n') {
            out.push_back(' ');
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

std::string sanitize_cpp_model_name(std::string_view raw_name) {
    std::string out;
    out.reserve(raw_name.size() + 2);
    for (char ch : raw_name) {
        if (is_ident_char(ch)) {
            out.push_back(ch);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "Fsm";
    } else if (std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return escape_cpp_keyword(out);
}

std::string resolve_action_cpp_name(const std::string& raw_name, const FsmIr& model) {
    if (raw_name.empty()) {
        return "";
    }
    bool collides = model.find_state(raw_name) != nullptr || model.is_choice_node(raw_name) ||
                    model.find_signal(raw_name) != nullptr;
    if (!collides) {
        for (const auto& t : model.transitions) {
            if (t.event == raw_name) {
                collides = true;
                break;
            }
        }
    }
    return collides ? escape_cpp_keyword(raw_name + "_action") : escape_cpp_keyword(raw_name);
}

bool has_unresolved_identifiers(std::string_view expr) {
    static const std::unordered_set<std::string_view> allowed = {
        "in",           "out",     "reg",    "cmd",         "srv",        "true",
        "false",        "nullptr", "sizeof", "static_cast", "const_cast", "reinterpret_cast",
        "dynamic_cast", "and",     "or",     "not",         "bitand",     "bitor",
        "xor"};
    const std::size_t n = expr.size();
    std::size_t i = 0;
    while (i < n) {
        if (expr[i] == '"' || expr[i] == '\'') {
            const char quote = expr[i++];
            while (i < n) {
                const char c = expr[i++];
                if (c == '\\' && i < n) {
                    ++i;
                } else if (c == quote) {
                    break;
                }
            }
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(expr[i])) != 0) {
            while (i < n && (is_ident_char(expr[i]) || expr[i] == '.')) {
                ++i;
            }
            continue;
        }
        if (is_ident_start(expr[i])) {
            const std::size_t start = i;
            while (i < n && is_ident_char(expr[i])) {
                ++i;
            }
            const std::string_view tok = expr.substr(start, i - start);
            std::size_t prev = start;
            while (prev > 0 && std::isspace(static_cast<unsigned char>(expr[prev - 1])) != 0) {
                --prev;
            }
            const bool preceded_by_access = (prev > 0 && expr[prev - 1] == '.') ||
                                            (prev >= 2 && expr[prev - 2] == '-' && expr[prev - 1] == '>') ||
                                            (prev >= 2 && expr[prev - 2] == ':' && expr[prev - 1] == ':');
            std::size_t next = i;
            while (next < n && std::isspace(static_cast<unsigned char>(expr[next])) != 0) {
                ++next;
            }
            const bool followed_by_call_or_scope =
                (next < n && expr[next] == '(') || (next + 1 < n && expr[next] == ':' && expr[next + 1] == ':');
            if (!preceded_by_access && !followed_by_call_or_scope && allowed.find(tok) == allowed.end()) {
                return true;
            }
            continue;
        }
        ++i;
    }
    return false;
}

bool has_self_constructor_call(std::string_view expr, std::string_view struct_name) {
    if (struct_name.empty()) {
        return false;
    }
    const std::size_t n = expr.size();
    std::size_t i = 0;
    while (i < n) {
        if (expr[i] == '"' || expr[i] == '\'') {
            const char quote = expr[i++];
            while (i < n) {
                const char c = expr[i++];
                if (c == '\\' && i < n) {
                    ++i;
                } else if (c == quote) {
                    break;
                }
            }
            continue;
        }
        if (is_ident_start(expr[i])) {
            const std::size_t start = i;
            while (i < n && is_ident_char(expr[i])) {
                ++i;
            }
            const std::string_view tok = expr.substr(start, i - start);
            if (tok == struct_name) {
                std::size_t prev = start;
                while (prev > 0 && std::isspace(static_cast<unsigned char>(expr[prev - 1])) != 0) {
                    --prev;
                }
                const bool preceded_by_access = (prev > 0 && expr[prev - 1] == '.') ||
                                                (prev >= 2 && expr[prev - 2] == '-' && expr[prev - 1] == '>') ||
                                                (prev >= 2 && expr[prev - 2] == ':' && expr[prev - 1] == ':');
                std::size_t next = i;
                while (next < n && std::isspace(static_cast<unsigned char>(expr[next])) != 0) {
                    ++next;
                }
                if (!preceded_by_access && next < n && expr[next] == '(') {
                    return true;
                }
            }
            continue;
        }
        ++i;
    }
    return false;
}

std::string qualify_enum_literal(std::string_view tok, const FsmIr& model, std::string_view preferred_enum_type) {
    if (!preferred_enum_type.empty()) {
        for (const auto& ct : model.custom_types) {
            if (ct.is_enum() && escape_cpp_keyword(ct.name) == preferred_enum_type) {
                for (const auto& lit : ct.literals) {
                    if (lit.name == tok) {
                        return escape_cpp_keyword(ct.name) + "::" + escape_cpp_keyword(lit.name);
                    }
                }
            }
        }
    }
    for (const auto& ct : model.custom_types) {
        if (!ct.is_enum()) {
            continue;
        }
        for (const auto& lit : ct.literals) {
            if (lit.name == tok) {
                return escape_cpp_keyword(ct.name) + "::" + escape_cpp_keyword(lit.name);
            }
        }
    }
    return "";
}

std::string qualify_expression(std::string expr, const FsmIr& model) {
    std::string out;
    out.reserve(expr.size() + 16);

    std::size_t i = 0;
    const std::size_t n = expr.size();
    while (i < n) {
        if (expr[i] == '"' || expr[i] == '\'') {
            char quote = expr[i];
            out.push_back(expr[i++]);
            while (i < n) {
                char c = expr[i++];
                out.push_back(c);
                if (c == '\\' && i < n) {
                    out.push_back(expr[i++]);
                } else if (c == quote) {
                    break;
                }
            }
            continue;
        }

        if (is_ident_start(expr[i])) {
            std::size_t start = i;
            while (i < n && is_ident_char(expr[i])) {
                ++i;
            }
            std::string tok = expr.substr(start, i - start);

            // Check preceding non-whitespace characters for '.', '->', or '::'
            std::size_t prev = start;
            while (prev > 0 && std::isspace(static_cast<unsigned char>(expr[prev - 1])) != 0) {
                --prev;
            }
            bool preceded_by_member_or_scope = false;
            if (prev > 0 && expr[prev - 1] == '.') {
                preceded_by_member_or_scope = true;
            } else if (prev >= 2 && expr[prev - 2] == '-' && expr[prev - 1] == '>') {
                preceded_by_member_or_scope = true;
            } else if (prev >= 2 && expr[prev - 2] == ':' && expr[prev - 1] == ':') {
                preceded_by_member_or_scope = true;
            }

            // Check following non-whitespace characters for '::' or '.'
            std::size_t next = i;
            while (next < n && std::isspace(static_cast<unsigned char>(expr[next])) != 0) {
                ++next;
            }
            bool followed_by_scope = (next + 1 < n && expr[next] == ':' && expr[next + 1] == ':');
            bool followed_by_dot = (next < n && expr[next] == '.');

            if (!preceded_by_member_or_scope && !followed_by_scope) {
                const std::string escaped_tok = escape_cpp_keyword(tok);
                if (model.find_variable(tok) != nullptr ||
                    (escaped_tok != tok && model.find_variable(escaped_tok) != nullptr)) {
                    out += "reg." + escaped_tok;
                    continue;
                }
                const auto* port = model.find_port(tok);
                if (port == nullptr && escaped_tok != tok) {
                    port = model.find_port(escaped_tok);
                }
                if (port != nullptr) {
                    if (port->is_in()) {
                        out += "in." + escaped_tok;
                        continue;
                    }
                    if (port->is_out()) {
                        out += "out." + escaped_tok;
                        continue;
                    }
                }
                if (followed_by_dot &&
                    (tok == "cmd" || tok == "event" || tok == "payload" || tok == "sig" || tok == "ev" || tok == "f" ||
                     std::any_of(model.signals.begin(), model.signals.end(),
                                 [&](const SignalDefinition& sig) { return sig.name == tok; }))) {
                    out += "cmd";
                    continue;
                }
                bool is_signal_attr = false;
                for (const auto& sig : model.signals) {
                    for (const auto& attr : sig.attributes) {
                        if (attr.name == tok || (escaped_tok != tok && attr.name == escaped_tok)) {
                            is_signal_attr = true;
                            break;
                        }
                    }
                    if (is_signal_attr) {
                        break;
                    }
                }
                if (is_signal_attr) {
                    out += "cmd." + escaped_tok;
                    continue;
                }
                std::string qualified_enum = qualify_enum_literal(tok, model);
                if (!qualified_enum.empty()) {
                    out += qualified_enum;
                    continue;
                }
            }

            if (prev > 0 && expr[prev - 1] == '.') {
                out += escape_cpp_keyword(tok);
                continue;
            }

            out += tok;
            continue;
        }

        out.push_back(expr[i++]);
    }
    return out;
}

bool has_member_access_prefix(const std::string& expr, std::initializer_list<std::string_view> prefixes) {
    std::size_t i = 0;
    const std::size_t n = expr.size();
    while (i < n) {
        if (expr[i] == '"' || expr[i] == '\'') {
            char quote = expr[i++];
            while (i < n) {
                char c = expr[i++];
                if (c == '\\' && i < n) {
                    ++i;
                } else if (c == quote) {
                    break;
                }
            }
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(expr[i])) != 0) {
            while (i < n && (is_ident_char(expr[i]) || expr[i] == '.')) {
                ++i;
            }
            continue;
        }
        if (is_ident_start(expr[i])) {
            std::size_t start = i;
            while (i < n && is_ident_char(expr[i])) {
                ++i;
            }
            std::string_view tok(expr.data() + start, i - start);
            bool preceded_by_dot_or_arrow = (start > 0 && expr[start - 1] == '.') ||
                                            (start >= 2 && expr[start - 2] == '-' && expr[start - 1] == '>') ||
                                            (start >= 2 && expr[start - 2] == ':' && expr[start - 1] == ':');
            if (!preceded_by_dot_or_arrow) {
                bool followed_by_member =
                    (i < n && expr[i] == '.') || (i + 1 < n && expr[i] == '-' && expr[i + 1] == '>');
                if (followed_by_member) {
                    for (auto p : prefixes) {
                        if (tok == p) {
                            return true;
                        }
                    }
                }
            }
            continue;
        }
        ++i;
    }
    return false;
}

EmittedSignalSummary collect_emitted_signals(const FsmIr& model) {
    EmittedSignalSummary summary;
    std::unordered_set<std::string> seen;
    std::size_t max_burst = 0;

    auto scan_action = [&](const ActionSignature& act) {
        std::size_t burst = 0;
        for (const auto& inst : act.instructions) {
            if (const auto* se = std::get_if<SignalEmitOp>(&inst.op)) {
                if (!se->signal_name.empty()) {
                    ++burst;
                    if (seen.insert(se->signal_name).second) {
                        summary.unique_signals.push_back(se->signal_name);
                    }
                }
            }
        }
        if (burst > max_burst) {
            max_burst = burst;
        }
    };

    for (const auto& st : model.states) {
        for (const auto& act : st.entry_actions) {
            scan_action(act);
        }
        for (const auto& act : st.exit_actions) {
            scan_action(act);
        }
    }
    for (const auto& tr : model.transitions) {
        if (tr.condition_action.has_value()) {
            scan_action(*tr.condition_action);
        }
        if (tr.transition_action.has_value()) {
            scan_action(*tr.transition_action);
        }
    }
    summary.max_outbox_capacity = std::max<std::size_t>(4U, max_burst * 2U);
    return summary;
}

}  // namespace fsm::backend::cpp::emitter_detail

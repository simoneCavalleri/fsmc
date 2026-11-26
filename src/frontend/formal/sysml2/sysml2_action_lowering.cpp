#include "sysml2_action_lowering.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

#include "fsm/frontend/formal/sysml2/sysml2_parser.hpp"
#include "fsm/ir/identifier_utils.hpp"

namespace fsm::frontend::formal::sysml2_detail {

using namespace fsm::ir;

bool try_parse_service_call_statement(const std::string& statement, const FsmIr& model,
                                      std::vector<ActionAstNode>& out_instructions) {
    if (model.services.empty()) {
        return false;
    }

    auto has_service = [&](const std::string& fn) {
        for (const auto& svc : model.services) {
            if (svc.name == fn) {
                return true;
            }
        }
        return false;
    };

    auto split_args = [](const std::string& args_str) {
        std::vector<std::string> args;
        std::string cur;
        int depth = 0;
        for (char c : args_str) {
            if (c == '(' || c == '[' || c == '{') {
                ++depth;
                cur.push_back(c);
            } else if (c == ')' || c == ']' || c == '}') {
                --depth;
                cur.push_back(c);
            } else if (c == ',' && depth == 0) {
                std::string t(trim(cur));
                if (!t.empty()) {
                    args.push_back(std::move(t));
                }
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        std::string t(trim(cur));
        if (!t.empty()) {
            args.push_back(std::move(t));
        }
        return args;
    };

    // Form 1: [assign] <lhs> :=|= <service_fn>(<args>)
    static const std::regex assigned_call_regex(
        R"(^(?:assign\s+)?([A-Za-z_][A-Za-z0-9_.]*)\s*:?=\s*([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)$)",
        std::regex::optimize);
    std::smatch m;
    if (std::regex_match(statement, m, assigned_call_regex)) {
        const std::string lhs_raw(trim(m[1].str()));
        const std::string fn(trim(m[2].str()));
        if (!has_service(fn)) {
            return false;
        }
        ActionCallOp call;
        call.function_name = fn;
        call.arguments = split_args(m[3].str());

        LValueTarget tgt(lhs_raw);
        if (lhs_raw.rfind("out.", 0) == 0) {
            tgt = LValueTarget(lhs_raw.substr(4), LValueScope::OutPort);
        } else if (lhs_raw.rfind("reg.", 0) == 0) {
            tgt = LValueTarget(lhs_raw.substr(4), LValueScope::Register);
        } else if (const auto* p = model.find_port(tgt.name); p != nullptr && p->is_out()) {
            tgt.scope = LValueScope::OutPort;
        } else {
            tgt.scope = LValueScope::Register;
        }
        call.result_target = std::move(tgt);
        out_instructions.emplace_back(std::move(call), statement);
        return true;
    }

    // Form 2: <service_fn>(<args>)
    static const std::regex void_call_regex(R"(^([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)$)", std::regex::optimize);
    if (std::regex_match(statement, m, void_call_regex)) {
        const std::string fn(trim(m[1].str()));
        if (!has_service(fn)) {
            return false;
        }
        ActionCallOp call;
        call.function_name = fn;
        call.arguments = split_args(m[2].str());
        out_instructions.emplace_back(std::move(call), statement);
        return true;
    }

    return false;
}

bool try_parse_assignment_statement(const std::string& statement, std::vector<ActionAssignment>& assignments,
                                    const FsmIr* model) {
    static const std::regex assign_regex(
        R"(^(?:assign\s+)?((?:(?:out|in|reg|local)\.)?)?(?:(?:service|context)\.)?([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s*(:=|=|\+=|-=|\*=|\/=)\s*(.+)$)",
        std::regex::optimize);
    static const std::regex inc_regex(
        R"(^((?:(?:out|in|reg|local)\.)?)?(?:(?:service|context)\.)?([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s*(\+\+|--)$)",
        std::regex::optimize);

    auto trim_ws = [](std::string_view sv) -> std::string {
        size_t first = sv.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return "";
        }
        size_t last = sv.find_last_not_of(" \t\r\n");
        return std::string(sv.substr(first, last - first + 1));
    };

    auto sanitize_dotted_path = [](const std::string& path) -> std::string {
        std::string res;
        std::size_t start = 0;
        while (start <= path.size()) {
            const std::size_t dot = path.find('.', start);
            if (!res.empty()) {
                res.push_back('.');
            }
            if (dot == std::string::npos) {
                res += sanitize_identifier(path.substr(start));
                break;
            }
            res += sanitize_identifier(path.substr(start, dot - start));
            start = dot + 1;
        }
        return res;
    };

    std::smatch assign_match;
    if (std::regex_match(statement, assign_match, assign_regex)) {
        std::string scope_prefix = assign_match[1].str();
        std::string var_path = sanitize_dotted_path(assign_match[2].str());
        std::string op = assign_match[3].str();
        std::string rhs = trim_ws(assign_match[4].str());
        LValueTarget tgt(scope_prefix + var_path);
        if (scope_prefix.empty() && model != nullptr) {
            if (tgt.member_path.size() == 1U) {
                const std::string& feat_name = tgt.member_path[0];
                if (const auto* p = model->find_port(feat_name);
                    p != nullptr && p->is_out() && p->description.rfind("port:" + tgt.name + ":", 0) == 0) {
                    tgt = LValueTarget(feat_name, LValueScope::OutPort);
                }
            } else if (const auto* p = model->find_port(tgt.name); p != nullptr && p->is_out()) {
                tgt.scope = LValueScope::OutPort;
            }
        }
        if (op == "=" || op == ":=") {
            assignments.push_back({tgt, rhs});
        } else if (op == "+=") {
            assignments.push_back({tgt, var_path + " + " + rhs});
        } else if (op == "-=") {
            assignments.push_back({tgt, var_path + " - " + rhs});
        } else if (op == "*=") {
            assignments.push_back({tgt, var_path + " * (" + rhs + ")"});
        } else if (op == "/=") {
            assignments.push_back({tgt, var_path + " / (" + rhs + ")"});
        }
        return true;
    }
    if (std::regex_match(statement, assign_match, inc_regex)) {
        std::string scope_prefix = assign_match[1].str();
        std::string var_path = sanitize_dotted_path(assign_match[2].str());
        std::string op = assign_match[3].str();
        LValueTarget tgt(scope_prefix + var_path);
        assignments.push_back({tgt, (op == "++") ? (var_path + " + 1") : (var_path + " - 1")});
        return true;
    }
    return false;
}

namespace {

std::string trim_sv(std::string_view sv) {
    const std::size_t first = sv.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return "";
    }
    const std::size_t last = sv.find_last_not_of(" \t\r\n");
    return std::string(sv.substr(first, last - first + 1));
}

std::vector<std::string> split_comma_top_level(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
            cur.push_back(c);
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
            cur.push_back(c);
        } else if (c == ',' && depth == 0) {
            std::string t = trim_sv(cur);
            if (!t.empty()) {
                out.push_back(std::move(t));
            }
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    std::string t = trim_sv(cur);
    if (!t.empty()) {
        out.push_back(std::move(t));
    }
    return out;
}

std::vector<std::string> order_named_bindings(const std::string& sig_name,
                                              const std::vector<std::pair<std::string, std::string>>& bindings,
                                              const FsmIr& model) {
    std::vector<std::string> result;
    if (bindings.empty()) {
        return result;
    }
    if (const auto* sig = model.find_signal(sig_name); sig != nullptr && !sig->attributes.empty()) {
        for (const auto& attr : sig->attributes) {
            bool found = false;
            for (const auto& [k, v] : bindings) {
                if (k == attr.name) {
                    result.push_back(v);
                    found = true;
                    break;
                }
            }
            if (!found && !attr.default_value.empty()) {
                result.push_back(attr.default_value);
            }
        }
        if (!result.empty()) {
            return result;
        }
    }
    for (const auto& [k, v] : bindings) {
        (void)k;
        result.push_back(v);
    }
    return result;
}

}  // namespace

std::vector<std::string> split_action_block_statements(std::string_view block) {
    std::vector<std::string> stmts;
    std::string cur;
    int brace_depth = 0;
    int paren_depth = 0;
    for (std::size_t i = 0; i < block.size(); ++i) {
        const char c = block[i];
        if (c == '(') {
            ++paren_depth;
            cur.push_back(c);
        } else if (c == ')') {
            if (paren_depth > 0) {
                --paren_depth;
            }
            cur.push_back(c);
        } else if (c == '{') {
            ++brace_depth;
            cur.push_back(c);
        } else if (c == '}') {
            if (brace_depth > 0) {
                --brace_depth;
            }
            cur.push_back(c);
            if (brace_depth == 0 && paren_depth == 0) {
                std::string t = trim_sv(cur);
                if (!t.empty()) {
                    stmts.push_back(std::move(t));
                }
                cur.clear();
            }
        } else if (c == ';' && brace_depth == 0 && paren_depth == 0) {
            std::string t = trim_sv(cur);
            if (!t.empty()) {
                stmts.push_back(std::move(t));
            }
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    std::string tail = trim_sv(cur);
    if (!tail.empty()) {
        stmts.push_back(std::move(tail));
    }
    return stmts;
}

bool extract_do_action_block(std::string_view stmt, std::string& out_explicit_name, std::string& out_body) {
    static const std::regex head_re(R"(\bdo\s*(?:action\s+)?(?:([A-Za-z_][A-Za-z0-9_]*)\s*)?\{)", std::regex::optimize);
    std::string text(stmt);
    std::smatch m;
    if (!std::regex_search(text, m, head_re)) {
        return false;
    }
    out_explicit_name = m[1].matched ? sanitize_identifier(m[1].str()) : "";
    const std::size_t open_brace = static_cast<std::size_t>(m.position() + m.length() - 1);
    int depth = 1;
    std::size_t pos = open_brace + 1;
    while (pos < text.size() && depth > 0) {
        if (text[pos] == '{') {
            ++depth;
        } else if (text[pos] == '}') {
            --depth;
        }
        ++pos;
    }
    if (depth != 0) {
        return false;
    }
    out_body = trim_sv(std::string_view(text).substr(open_brace + 1, (pos - 1) - (open_brace + 1)));
    return true;
}

bool try_parse_local_item_usage(const std::string& statement, LocalItemMap& local_items) {
    static const std::regex item_re(
        R"(^item\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_:]*)(?:\s*\{([\s\S]*)\})?$)",
        std::regex::optimize);
    std::smatch m;
    if (!std::regex_match(statement, m, item_re)) {
        return false;
    }
    LocalItemSpec spec;
    const std::string var_name = sanitize_identifier(m[1].str());
    std::string raw_type = m[2].str();
    const auto sep = raw_type.rfind("::");
    if (sep != std::string::npos) {
        raw_type = raw_type.substr(sep + 2);
    }
    spec.type_name = sanitize_identifier(raw_type);

    if (m[3].matched) {
        const std::string body = m[3].str();
        static const std::regex field_re(
            R"(^(?:attribute\s+)?(?::>>\s*|redefines\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*:?=\s*(.+)$)",
            std::regex::optimize);
        std::stringstream ss(body);
        std::string part;
        while (std::getline(ss, part, ';')) {
            part = trim_sv(part);
            if (part.empty()) {
                continue;
            }
            std::smatch fm;
            if (std::regex_match(part, fm, field_re)) {
                spec.field_bindings.emplace_back(sanitize_identifier(fm[1].str()), trim_sv(fm[2].str()));
            }
        }
    }
    local_items[var_name] = std::move(spec);
    return true;
}

bool try_parse_send_statement(const std::string& statement, const LocalItemMap& local_items, FsmIr& model,
                              SignalEmitOp& out_emit, std::string& out_action_name, std::string& out_invocation) {
    static const std::regex send_re(
        R"(^(?:do\s+)?send\s+(new\s+)?([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(([^)]*)\))?\s+(?:via|to)\s+([A-Za-z_][A-Za-z0-9_.]*)$)",
        std::regex::optimize);
    std::smatch sm;
    if (!std::regex_match(statement, sm, send_re)) {
        return false;
    }

    const bool has_new = sm[1].matched;
    const std::string target_ident = sanitize_identifier(sm[2].str());
    const bool has_parens = sm[3].matched;
    const std::string raw_args = has_parens ? trim_sv(sm[3].str()) : "";
    std::string raw_port = sm[4].str();
    const auto dot = raw_port.find('.');
    if (dot != std::string::npos) {
        raw_port = raw_port.substr(0, dot);
    }
    const std::string port_name = sanitize_identifier(raw_port);

    std::string sig_name = target_ident;
    std::vector<std::string> resolved_args;

    if (!has_new && !has_parens) {
        if (auto it = local_items.find(target_ident); it != local_items.end()) {
            sig_name = it->second.type_name;
            resolved_args = order_named_bindings(sig_name, it->second.field_bindings, model);
        }
    } else if (!raw_args.empty()) {
        const auto parts = split_comma_top_level(raw_args);
        std::vector<std::pair<std::string, std::string>> named_bindings;
        bool all_named = !parts.empty();
        static const std::regex named_arg_re(R"(^([A-Za-z_][A-Za-z0-9_]*)\s*:?=\s*(.+)$)", std::regex::optimize);
        for (const auto& p : parts) {
            std::smatch nm;
            if (std::regex_match(p, nm, named_arg_re)) {
                named_bindings.emplace_back(sanitize_identifier(nm[1].str()), trim_sv(nm[2].str()));
            } else {
                all_named = false;
                break;
            }
        }
        if (all_named) {
            resolved_args = order_named_bindings(sig_name, named_bindings, model);
        } else {
            resolved_args = parts;
        }
    }

    if (model.find_port(port_name) == nullptr) {
        model.ports.emplace_back(port_name, DataType{PrimitiveTypeKind::UInt32}, PortDirection::Out);
    }
    if (model.find_signal(sig_name) == nullptr) {
        model.signals.emplace_back(sig_name);
    }

    std::string joined_args;
    for (std::size_t i = 0; i < resolved_args.size(); ++i) {
        if (i > 0) {
            joined_args += ", ";
        }
        joined_args += resolved_args[i];
    }

    out_action_name = "send_" + sig_name + "_via_" + port_name;
    out_invocation = port_name + ".send(" + sig_name + (joined_args.empty() ? "" : (", " + joined_args)) + ")";
    out_emit = SignalEmitOp{};
    out_emit.signal_name = sig_name;
    out_emit.target_port = port_name;
    out_emit.arguments = std::move(resolved_args);
    return true;
}

}  // namespace fsm::frontend::formal::sysml2_detail

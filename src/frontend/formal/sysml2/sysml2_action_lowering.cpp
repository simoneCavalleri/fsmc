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
            if (const auto* p = model->find_port(tgt.name); p != nullptr && p->is_out()) {
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

}  // namespace fsm::frontend::formal::sysml2_detail

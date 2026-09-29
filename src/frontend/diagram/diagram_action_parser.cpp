/**
 * @file diagram_action_parser.cpp
 * @brief Implementation of composite and multiline action block parsing for diagrams.
 */

#include "fsm/frontend/diagram/diagram_action_parser.hpp"

#include <cctype>
#include <regex>

namespace fsm::frontend::diagram {

namespace {

std::string trim_str(std::string_view sv) {
    size_t s = 0;
    while (s < sv.size() && std::isspace(static_cast<unsigned char>(sv[s])) != 0)
        s++;
    if (s == sv.size())
        return "";
    size_t e = sv.size() - 1;
    while (e > s && std::isspace(static_cast<unsigned char>(sv[e])) != 0)
        e--;
    return std::string(sv.substr(s, e - s + 1));
}

std::string sanitize_id(std::string_view raw) {
    std::string res;
    res.reserve(raw.size());
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
            res.push_back(c);
        }
    }
    return res;
}

}  // namespace

int DiagramActionParser::brace_imbalance(std::string_view line) {
    int imbalance = 0;
    bool in_quote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"' && (i == 0 || line[i - 1] != '\\')) {
            in_quote = !in_quote;
        } else if (!in_quote) {
            if (c == '{') {
                imbalance++;
            } else if (c == '}') {
                imbalance--;
            }
        }
    }
    return imbalance;
}

ir::ActionSignature DiagramActionParser::parse_action_block(std::string_view raw_act,
                                                            const std::string& fallback_name) {
    ir::ActionSignature sig;
    std::string act = trim_str(raw_act);
    if (act.empty()) {
        return sig;
    }

    // Strip enclosing braces: { ... }
    if (act.front() == '{' && act.back() == '}') {
        act = trim_str(act.substr(1, act.size() - 2));
    }

    // Single simple identifier without semicolons, assignments, parens, or whitespace
    if (act.find(';') == std::string::npos && act.find('=') == std::string::npos &&
        act.find('(') == std::string::npos && act.find(' ') == std::string::npos &&
        act.find('\n') == std::string::npos && act.find('\t') == std::string::npos) {
        sig.name = sanitize_id(act);
        sig.invocation = sig.name;
        return sig;
    }

    // Split into individual statements separated by ';'
    std::vector<std::string> statements;
    std::string current;
    int brace_depth = 0;
    bool in_quote = false;

    for (size_t i = 0; i < act.size(); ++i) {
        char c = act[i];
        if (c == '"' && (i == 0 || act[i - 1] != '\\')) {
            in_quote = !in_quote;
            current += c;
        } else if (!in_quote && c == '{') {
            brace_depth++;
            current += c;
        } else if (!in_quote && c == '}') {
            if (brace_depth > 0)
                brace_depth--;
            current += c;
        } else if (!in_quote && brace_depth == 0 && c == ';') {
            std::string stmt = trim_str(current);
            if (!stmt.empty()) {
                statements.push_back(stmt);
            }
            current.clear();
        } else {
            current += c;
        }
    }
    std::string last = trim_str(current);
    if (!last.empty()) {
        statements.push_back(last);
    }

    static const std::regex assign_regex(
        R"(^(?:(?:out|in|reg|service|context)\.)?([A-Za-z_][A-Za-z0-9_.]*(?:\[\d+\])?)\s*(=|\+=|-=|\*=|/=|%=|<<=|>>=|&=|\|=|\^=)\s*(.+)$)",
        std::regex::optimize);
    static const std::regex inc_regex(
        R"(^(?:(?:out|in|reg|service|context)\.)?([A-Za-z_][A-Za-z0-9_.]*)\s*(\+\+|--)$)", std::regex::optimize);

    std::vector<std::string> call_names;

    for (const auto& stmt : statements) {
        std::smatch match;
        if (std::regex_match(stmt, match, assign_regex)) {
            ir::ActionAssignment asgn = ir::ActionAssignment::parse(stmt);
            sig.assignments.push_back(std::move(asgn));
        } else if (std::regex_match(stmt, match, inc_regex)) {
            std::string var = match[1].str();
            std::string op = match[2].str();
            std::string desugared = var + (op == "++" ? " += 1" : " -= 1");
            ir::ActionAssignment asgn = ir::ActionAssignment::parse(desugared);
            sig.assignments.push_back(std::move(asgn));
        } else {
            std::string call = sanitize_id(stmt);
            if (!call.empty()) {
                call_names.push_back(call);
                ir::ActionCallOp call_op;
                call_op.function_name = call;
                sig.instructions.emplace_back(std::move(call_op), stmt);
            }
        }
    }

    if (!sig.assignments.empty()) {
        if (sig.assignments.size() == 1 && call_names.empty()) {
            sig.name = "assign_" + sig.assignments[0].target.name;
        } else {
            sig.name = fallback_name.empty() ? "action_composite" : fallback_name;
        }
    } else if (!call_names.empty()) {
        sig.name = call_names.front();
    } else {
        sig.name = sanitize_id(act);
    }
    sig.invocation = act;
    return sig;
}

}  // namespace fsm::frontend::diagram

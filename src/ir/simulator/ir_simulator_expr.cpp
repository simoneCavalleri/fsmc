#include "ir_simulator_expr.hpp"

#include <cctype>
#include <sstream>

#include "fsm/ir/expression.hpp"

namespace fsm::ir::simulator_detail {

std::string trim_sv(std::string_view sv) {
    std::size_t s = 0;
    while (s < sv.size() && std::isspace(static_cast<unsigned char>(sv[s])) != 0) {
        ++s;
    }
    std::size_t e = sv.size();
    while (e > s && std::isspace(static_cast<unsigned char>(sv[e - 1])) != 0) {
        --e;
    }
    return std::string(sv.substr(s, e - s));
}

std::vector<std::string> split_top_level(std::string_view sv, std::string_view delim) {
    std::vector<std::string> parts;
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '(' || sv[i] == '{' || sv[i] == '[') {
            ++depth;
        } else if (sv[i] == ')' || sv[i] == '}' || sv[i] == ']') {
            --depth;
        } else if (depth == 0 && i + delim.size() <= sv.size() && sv.substr(i, delim.size()) == delim) {
            parts.push_back(trim_sv(sv.substr(start, i - start)));
            i += delim.size() - 1;
            start = i + 1;
        }
    }
    parts.push_back(trim_sv(sv.substr(start)));
    return parts;
}

bool eval_numeric_predicate(std::string_view expr, const std::unordered_map<std::string, double>& env,
                            bool& symbolic_fallback, const FsmIr* model, bool strict_fail_closed) {
    std::string s = trim_sv(expr);
    if (s.empty() || s == "true" || s == "else" || s == "otherwise" || s == "default") {
        return true;
    }
    if (s == "false") {
        return false;
    }
    // Handle logical OR / AND
    if (auto or_parts = split_top_level(s, "||"); or_parts.size() > 1) {
        for (const auto& p : or_parts) {
            if (eval_numeric_predicate(p, env, symbolic_fallback, model, strict_fail_closed)) {
                return true;
            }
        }
        return false;
    }
    if (auto and_parts = split_top_level(s, "&&"); and_parts.size() > 1) {
        for (const auto& p : and_parts) {
            if (!eval_numeric_predicate(p, env, symbolic_fallback, model, strict_fail_closed)) {
                return false;
            }
        }
        return true;
    }
    // Strip outer parentheses if balanced across the entire expression
    if (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        int depth = 0;
        bool wraps_all = true;
        for (std::size_t i = 0; i + 1 < s.size(); ++i) {
            if (s[i] == '(') {
                ++depth;
            } else if (s[i] == ')') {
                --depth;
            }
            if (depth == 0) {
                wraps_all = false;
                break;
            }
        }
        if (wraps_all) {
            return eval_numeric_predicate(std::string_view(s).substr(1, s.size() - 2), env, symbolic_fallback, model,
                                          strict_fail_closed);
        }
    }
    // Handle C++ guard combinators (fsm::and_<...>, fsm::or_<...>, fsm::not_<...>)
    auto match_combinator = [&](std::string_view prefix, std::string_view& inner_out) -> bool {
        if (s.size() > prefix.size() + 1 && s.rfind(prefix, 0) == 0 && s.back() == '>') {
            inner_out = std::string_view(s).substr(prefix.size(), s.size() - prefix.size() - 1);
            return true;
        }
        return false;
    };
    auto split_angle_comma = [](std::string_view sv) -> std::vector<std::string> {
        std::vector<std::string> out;
        int depth = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i < sv.size(); ++i) {
            if (sv[i] == '<' || sv[i] == '(') {
                ++depth;
            } else if ((sv[i] == '>' || sv[i] == ')') && depth > 0) {
                --depth;
            } else if (depth == 0 && sv[i] == ',') {
                out.push_back(trim_sv(sv.substr(start, i - start)));
                start = i + 1;
            }
        }
        out.push_back(trim_sv(sv.substr(start)));
        return out;
    };
    std::string_view comb_inner;
    if (match_combinator("::fsm::and_<", comb_inner) || match_combinator("fsm::and_<", comb_inner)) {
        for (const auto& p : split_angle_comma(comb_inner)) {
            if (!eval_numeric_predicate(p, env, symbolic_fallback, model, strict_fail_closed)) {
                return false;
            }
        }
        return true;
    }
    if (match_combinator("::fsm::or_<", comb_inner) || match_combinator("fsm::or_<", comb_inner)) {
        for (const auto& p : split_angle_comma(comb_inner)) {
            if (eval_numeric_predicate(p, env, symbolic_fallback, model, strict_fail_closed)) {
                return true;
            }
        }
        return false;
    }
    if (match_combinator("::fsm::not_<", comb_inner) || match_combinator("fsm::not_<", comb_inner)) {
        return !eval_numeric_predicate(comb_inner, env, symbolic_fallback, model, strict_fail_closed);
    }
    // Resolve named GuardModel from FsmIr::guards if present
    if (model != nullptr) {
        for (const auto& gsig : model->guards) {
            if (gsig.name == s) {
                std::string resolved;
                if (gsig.normalized_expression.has_value() && !gsig.normalized_expression->empty()) {
                    resolved = *gsig.normalized_expression;
                } else if (gsig.raw_expression.has_value() && !gsig.raw_expression->empty()) {
                    resolved = *gsig.raw_expression;
                }
                if (!resolved.empty() && resolved != s) {
                    return eval_numeric_predicate(resolved, env, symbolic_fallback, model, strict_fail_closed);
                }
                break;
            }
        }
    }

    auto ast = ExpressionAstNode::parse(s);
    if (ast.kind != ExpressionKind::RawExpression) {
        if (auto b = ast.evaluate_boolean(env); b.has_value()) {
            return *b;
        }
    }

    auto resolve_val = [&](const std::string& tok, double& out_val) -> bool {
        if (tok == "true") {
            out_val = 1.0;
            return true;
        }
        if (tok == "false") {
            out_val = 0.0;
            return true;
        }
        if (auto it = env.find(tok); it != env.end()) {
            out_val = it->second;
            return true;
        }
        if (auto dot = tok.rfind('.'); dot != std::string::npos) {
            if (auto it = env.find(tok.substr(dot + 1)); it != env.end()) {
                out_val = it->second;
                return true;
            }
        }
        try {
            std::size_t idx = 0;
            double v = std::stod(tok, &idx);
            if (idx == tok.size()) {
                out_val = v;
                return true;
            }
        } catch (...) {
        }
        return false;
    };

    for (std::string_view op : {"<=", ">=", "==", "!=", "<", ">"}) {
        if (auto parts = split_top_level(s, op); parts.size() == 2) {
            double lhs = 0.0;
            double rhs = 0.0;
            if (resolve_val(parts[0], lhs) && resolve_val(parts[1], rhs)) {
                if (op == "<=") {
                    return lhs <= rhs;
                }
                if (op == ">=") {
                    return lhs >= rhs;
                }
                if (op == "==") {
                    return lhs == rhs;
                }
                if (op == "!=") {
                    return lhs != rhs;
                }
                if (op == "<") {
                    return lhs < rhs;
                }
                if (op == ">") {
                    return lhs > rhs;
                }
            }
            symbolic_fallback = true;
            return !strict_fail_closed;
        }
    }

    double single = 0.0;
    if (resolve_val(s, single)) {
        return single != 0.0;
    }
    symbolic_fallback = true;
    return !strict_fail_closed;
}

std::vector<SimulatedEvent> parse_trace_input(const std::string& content) {
    std::vector<SimulatedEvent> events;
    std::string trimmed = trim_sv(content);
    if (trimmed.empty()) {
        return events;
    }
    if (trimmed.front() == '[') {
        // Lightweight JSON array parser for ["Ev1", ...] or [{"event":"Packet","payload":{"len":64}}, ...]
        std::string_view inner(trimmed.data() + 1, trimmed.size() - 2);
        auto items = split_top_level(inner, ",");
        for (const auto& item : items) {
            if (item.empty()) {
                continue;
            }
            SimulatedEvent ev;
            if (item.front() == '"' && item.back() == '"' && item.size() >= 2) {
                ev.name = item.substr(1, item.size() - 2);
                events.push_back(std::move(ev));
            } else if (item.front() == '{' && item.back() == '}') {
                std::string_view obj(item.data() + 1, item.size() - 2);
                for (const auto& kv : split_top_level(obj, ",")) {
                    auto colon = kv.find(':');
                    if (colon == std::string::npos) {
                        continue;
                    }
                    std::string k = trim_sv(std::string_view(kv).substr(0, colon));
                    std::string v = trim_sv(std::string_view(kv).substr(colon + 1));
                    if (k.size() >= 2 && k.front() == '"' && k.back() == '"') {
                        k = k.substr(1, k.size() - 2);
                    }
                    if ((k == "event" || k == "name") && v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                        ev.name = v.substr(1, v.size() - 2);
                    } else if (k == "payload" && v.size() >= 2 && v.front() == '{' && v.back() == '}') {
                        std::string_view p_obj(v.data() + 1, v.size() - 2);
                        for (const auto& pkv : split_top_level(p_obj, ",")) {
                            auto pcol = pkv.find(':');
                            if (pcol == std::string::npos) {
                                continue;
                            }
                            std::string pk = trim_sv(std::string_view(pkv).substr(0, pcol));
                            std::string pv = trim_sv(std::string_view(pkv).substr(pcol + 1));
                            if (pk.size() >= 2 && pk.front() == '"' && pk.back() == '"') {
                                pk = pk.substr(1, pk.size() - 2);
                            }
                            if (pv == "true") {
                                ev.numeric_payload[pk] = 1.0;
                            } else if (pv == "false") {
                                ev.numeric_payload[pk] = 0.0;
                            } else if (pv.size() >= 2 && pv.front() == '"' && pv.back() == '"') {
                                ev.string_payload[pk] = pv.substr(1, pv.size() - 2);
                            } else {
                                try {
                                    ev.numeric_payload[pk] = std::stod(pv);
                                } catch (...) {
                                    ev.string_payload[pk] = pv;
                                }
                            }
                        }
                    }
                }
                if (!ev.name.empty()) {
                    events.push_back(std::move(ev));
                }
            }
        }
        return events;
    }

    // Line-based trace: "EventName" or "EventName key=val ..."
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        std::string t = trim_sv(line);
        if (t.empty() || t[0] == '#') {
            continue;
        }
        std::istringstream lss(t);
        SimulatedEvent ev;
        lss >> ev.name;
        std::string tok;
        while (lss >> tok) {
            auto eq = tok.find('=');
            if (eq != std::string::npos) {
                std::string k = tok.substr(0, eq);
                std::string v = tok.substr(eq + 1);
                try {
                    ev.numeric_payload[k] = std::stod(v);
                } catch (...) {
                    ev.string_payload[k] = v;
                }
            }
        }
        if (!ev.name.empty()) {
            events.push_back(std::move(ev));
        }
    }
    return events;
}

}  // namespace fsm::ir::simulator_detail

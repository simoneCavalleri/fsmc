#pragma once

#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/model_checker.hpp"

namespace fsm::tools {

struct SimulatedEvent {
    std::string name;
    std::unordered_map<std::string, double> numeric_payload;
    std::unordered_map<std::string, std::string> string_payload;
};

namespace detail {

inline std::string trim_sv(std::string_view sv) {
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

inline std::vector<std::string> split_top_level(std::string_view sv, std::string_view delim) {
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

inline bool eval_numeric_predicate(std::string_view expr, const std::unordered_map<std::string, double>& env,
                                   bool& symbolic_fallback, const fsm::ir::FsmIr* model = nullptr) {
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
            if (eval_numeric_predicate(p, env, symbolic_fallback, model)) {
                return true;
            }
        }
        return false;
    }
    if (auto and_parts = split_top_level(s, "&&"); and_parts.size() > 1) {
        for (const auto& p : and_parts) {
            if (!eval_numeric_predicate(p, env, symbolic_fallback, model)) {
                return false;
            }
        }
        return true;
    }
    // Strip outer parentheses if balanced
    if (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        return eval_numeric_predicate(std::string_view(s).substr(1, s.size() - 2), env, symbolic_fallback, model);
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
            if (!eval_numeric_predicate(p, env, symbolic_fallback, model)) {
                return false;
            }
        }
        return true;
    }
    if (match_combinator("::fsm::or_<", comb_inner) || match_combinator("fsm::or_<", comb_inner)) {
        for (const auto& p : split_angle_comma(comb_inner)) {
            if (eval_numeric_predicate(p, env, symbolic_fallback, model)) {
                return true;
            }
        }
        return false;
    }
    if (match_combinator("::fsm::not_<", comb_inner) || match_combinator("fsm::not_<", comb_inner)) {
        return !eval_numeric_predicate(comb_inner, env, symbolic_fallback, model);
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
                    return eval_numeric_predicate(resolved, env, symbolic_fallback, model);
                }
                break;
            }
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
                if (op == "<=")
                    return lhs <= rhs;
                if (op == ">=")
                    return lhs >= rhs;
                if (op == "==")
                    return lhs == rhs;
                if (op == "!=")
                    return lhs != rhs;
                if (op == "<")
                    return lhs < rhs;
                if (op == ">")
                    return lhs > rhs;
            }
            symbolic_fallback = true;
            return true;
        }
    }

    double single = 0.0;
    if (resolve_val(s, single)) {
        return single != 0.0;
    }
    symbolic_fallback = true;
    return true;
}

inline std::vector<SimulatedEvent> parse_trace_input(const std::string& content) {
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
                    if (colon == std::string::npos)
                        continue;
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
                            if (pcol == std::string::npos)
                                continue;
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
        if (t.empty() || t[0] == '#')
            continue;
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

}  // namespace detail

class IrSimulator {
  public:
    static std::string simulate_trace(const fsm::ir::FsmIr& model, const std::string& trace_content,
                                      std::ostream& out) {
        std::unordered_map<std::string, double> env;
        for (const auto& var : model.variables) {
            if (!var.initial_value.empty()) {
                try {
                    env[var.name] = std::stod(var.initial_value);
                } catch (...) {
                    env[var.name] = 0.0;
                }
            } else {
                env[var.name] = 0.0;
            }
        }

        std::string current_state = model.initial_state;
        if (current_state.empty() && !model.states.empty()) {
            current_state = model.states.front().name;
        }
        // Descend into initial substate if composite
        while (const auto* st = model.find_state(current_state)) {
            if (!st->initial_sub_state.empty()) {
                current_state = st->initial_sub_state;
            } else {
                break;
            }
        }

        out << "============================================================================\n"
            << " fsmc IR Simulator : " << model.name << "\n"
            << " Initial State     : " << current_state << "\n"
            << "----------------------------------------------------------------------------\n";

        auto events = detail::parse_trace_input(trace_content);
        for (std::size_t step = 0; step < events.size(); ++step) {
            const auto& ev = events[step];
            auto step_env = env;
            for (const auto& [k, v] : ev.numeric_payload) {
                step_env[k] = v;
                step_env["payload." + k] = v;
                step_env["cmd." + k] = v;
                step_env["event." + k] = v;
            }

            // 1. Safe payload rejection check against SignalDefinition::validators
            if (const auto* sig = model.find_signal(ev.name)) {
                bool payload_valid = true;
                for (const auto& val_expr : sig->validators) {
                    bool sym = false;
                    if (!detail::eval_numeric_predicate(val_expr, step_env, sym)) {
                        payload_valid = false;
                        break;
                    }
                }
                if (!payload_valid) {
                    out << "[STEP " << step << "] " << current_state << " --(" << ev.name
                        << ")--> [REJECTED: invalid signal payload]\n";
                    continue;
                }
            }

            // 2. Match transition along active state hierarchy
            const fsm::ir::TransitionEdge* matched = nullptr;
            bool used_symbolic_guard = false;
            std::string probe = current_state;
            while (!probe.empty() && matched == nullptr) {
                for (const auto& tr : model.transitions) {
                    if (tr.source != probe)
                        continue;
                    if (tr.get_trigger_name() != ev.name)
                        continue;
                    bool sym = false;
                    auto resolve_guard_atom = [&](const std::string& expr) -> bool {
                        bool atom_sym = false;
                        bool ok = detail::eval_numeric_predicate(expr, step_env, atom_sym, &model);
                        if (atom_sym) {
                            sym = true;
                        }
                        return ok;
                    };
                    auto eval_guard_ast = [&](auto& self, const fsm::ir::GuardAstNode& node) -> bool {
                        if (node.op == fsm::ir::GuardOp::None) {
                            return resolve_guard_atom(node.expression);
                        }
                        if (node.op == fsm::ir::GuardOp::Not && node.children.size() == 1) {
                            return !self(self, node.children[0]);
                        }
                        if (node.op == fsm::ir::GuardOp::And && !node.children.empty()) {
                            for (const auto& ch : node.children) {
                                if (!self(self, ch)) {
                                    return false;
                                }
                            }
                            return true;
                        }
                        if (node.op == fsm::ir::GuardOp::Or && !node.children.empty()) {
                            for (const auto& ch : node.children) {
                                if (self(self, ch)) {
                                    return true;
                                }
                            }
                            return false;
                        }
                        return resolve_guard_atom(node.expression);
                    };
                    if (const auto* st_trig = std::get_if<fsm::ir::SignalTrigger>(&tr.trigger)) {
                        if (!st_trig->payload_binding.empty()) {
                            for (const auto& [k, v] : ev.numeric_payload) {
                                step_env[st_trig->payload_binding + "." + k] = v;
                            }
                        }
                    }
                    bool guard_passed = false;
                    if (tr.guard_ast.has_value() && tr.guard_ast->op != fsm::ir::GuardOp::None) {
                        guard_passed = eval_guard_ast(eval_guard_ast, *tr.guard_ast);
                    } else {
                        guard_passed = resolve_guard_atom(tr.guard.value_or(""));
                    }
                    if (guard_passed) {
                        matched = &tr;
                        used_symbolic_guard = sym;
                        break;
                    }
                }
                if (matched == nullptr) {
                    const auto* st = model.find_state(probe);
                    probe = (st != nullptr) ? st->parent_state : "";
                }
            }

            if (matched == nullptr) {
                out << "[STEP " << step << "] " << current_state << " --(" << ev.name << ")--> [UNHANDLED]\n";
                continue;
            }

            auto apply_act = [&](const std::optional<fsm::ir::ActionSignature>& act_opt) {
                if (!act_opt.has_value())
                    return;
                for (const auto& assign : act_opt->assignments) {
                    double rhs = 0.0;
                    bool sym = false;
                    if (auto it = step_env.find(assign.expression); it != step_env.end()) {
                        rhs = it->second;
                    } else {
                        try {
                            rhs = std::stod(assign.expression);
                        } catch (...) {
                            sym = true;
                        }
                    }
                    if (!sym) {
                        if (assign.op == fsm::ir::AssignmentOp::Assign)
                            env[assign.target.name] = rhs;
                        else if (assign.op == fsm::ir::AssignmentOp::AddAssign)
                            env[assign.target.name] += rhs;
                        else if (assign.op == fsm::ir::AssignmentOp::SubAssign)
                            env[assign.target.name] -= rhs;
                    }
                }
            };
            apply_act(matched->condition_action);
            apply_act(matched->transition_action);

            std::string next_state = matched->target;
            while (const auto* st = model.find_state(next_state)) {
                if (!st->initial_sub_state.empty()) {
                    next_state = st->initial_sub_state;
                } else {
                    break;
                }
            }

            out << "[STEP " << step << "] " << current_state << " --(" << ev.name;
            if (!ev.numeric_payload.empty()) {
                out << " {";
                bool first = true;
                for (const auto& [k, v] : ev.numeric_payload) {
                    if (!first)
                        out << ", ";
                    out << k << "=" << v;
                    first = false;
                }
                out << "}";
            }
            out << ")--> " << next_state;
            if (used_symbolic_guard) {
                out << " [symbolic-guard]";
            }
            out << "\n";
            current_state = std::move(next_state);
        }

        out << "----------------------------------------------------------------------------\n"
            << " Final State       : " << current_state << "\n"
            << "============================================================================\n";
        return current_state;
    }
};

class CounterexampleGTestExporter {
  public:
    static std::string export_gtest(const fsm::ir::FsmIr& model,
                                    const fsm::middleend::analysis::ModelCheckResult& result,
                                    std::string_view header_include = "") {
        std::ostringstream out;
        std::string hdr = header_include.empty() ? (model.name + "_fsm.hpp") : std::string(header_include);
        out << "// Auto-generated by fsmc --export-cex-gtest\n"
            << "#include <gtest/gtest.h>\n"
            << "#include \"" << hdr << "\"\n\n";

        std::string test_name = result.property_name.empty() ? "Counterexample" : result.property_name;
        for (char& c : test_name) {
            if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
                c = '_';
            }
        }

        out << "TEST(" << model.name << "CounterexampleTest, Reproduces_" << test_name << ") {\n"
            << "    // Violated property: " << result.property_formula << "\n"
            << "    " << model.name << "FSM sm;\n";

        std::string final_state;
        for (const auto& step : result.counterexample_trace) {
            if (!step.state_name.empty()) {
                final_state = step.state_name;
            }
            if (!step.event_name.empty() && step.event_name != "anonymous_event" &&
                step.event_name != "completion_event") {
                out << "    sm.dispatch(" << step.event_name << "{});\n";
            }
        }
        if (!final_state.empty()) {
            out << "    EXPECT_TRUE(sm.is_in<" << final_state << ">());\n";
        }
        out << "}\n";
        return out.str();
    }
};

}  // namespace fsm::tools

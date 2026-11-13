#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <queue>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace fsm::middleend::analysis {

using namespace fsm::ir;
using namespace fsm::diagnostic;

std::string Interval::to_string() const {
    if (is_empty())
        return "[empty]";
    std::ostringstream oss;
    oss << "[";
    if (std::isinf(lo) && lo < 0) {
        oss << "-inf";
    } else {
        oss << lo;
    }
    oss << ", ";
    if (std::isinf(hi) && hi > 0) {
        oss << "+inf";
    } else {
        oss << hi;
    }
    oss << "]";
    return oss.str();
}

std::unordered_map<std::string, std::unordered_map<std::string, Interval>>
EFSMIntervalAnalyzer::compute_state_intervals() {
    std::unordered_map<std::string, std::unordered_map<std::string, Interval>> state_envs;

    if (ir_.variables.empty() && ir_.ports.empty()) {
        return state_envs;
    }

    // 1. Initialize environment for initial state
    std::unordered_map<std::string, Interval> init_env;

    for (const auto& var : ir_.variables) {
        if (!var.initial_value.empty()) {
            try {
                double val = std::stod(var.initial_value);
                init_env[var.name] = Interval(val, val);
            } catch (...) {
                init_env[var.name] = Interval();
            }
        } else {
            init_env[var.name] = Interval();
        }
    }

    for (const auto& port : ir_.ports) {
        if (!port.default_value.empty()) {
            try {
                double val = std::stod(port.default_value);
                init_env[port.name] = Interval(val, val);
                continue;
            } catch (...) {
            }
        }
        if (port.min_value.has_value() || port.max_value.has_value()) {
            double lo = port.min_value.value_or(-std::numeric_limits<double>::infinity());
            double hi = port.max_value.value_or(std::numeric_limits<double>::infinity());
            init_env[port.name] = Interval(lo, hi);
        } else {
            init_env[port.name] = Interval();
        }
    }

    std::string root =
        ir_.initial_state.empty() ? (ir_.states.empty() ? "" : ir_.states.front().name) : ir_.initial_state;
    if (root.empty()) {
        return state_envs;
    }

    state_envs[root] = init_env;

    // 2. Fixed-point iteration with worklist
    std::queue<std::string> worklist;
    std::unordered_set<std::string> in_worklist;
    std::unordered_map<std::string, std::size_t> state_visits;
    worklist.push(root);
    in_worklist.insert(root);

    std::size_t iterations = 0;
    constexpr std::size_t kMaxIterations = 200;

    while (!worklist.empty() && iterations++ < kMaxIterations) {
        std::string curr_state = worklist.front();
        worklist.pop();
        in_worklist.erase(curr_state);
        state_visits[curr_state]++;

        const auto curr_env = state_envs[curr_state];

        // Inspect all outgoing transitions from curr_state
        for (const auto& t : ir_.transitions) {
            if (t.source != curr_state) {
                continue;
            }

            // Propagate variable and port assignments across the transition
            auto next_env = curr_env;
            std::unordered_set<std::string> ephemeral_payload_keys;
            if (const auto* sig_trig = std::get_if<SignalTrigger>(&t.trigger)) {
                if (const auto* sig_def = ir_.find_signal(sig_trig->signal_name); sig_def != nullptr) {
                    for (const auto& attr : sig_def->attributes) {
                        Interval attr_iv;
                        for (const auto& val_expr : sig_def->validators) {
                            if (auto bound = parse_guard_domain(val_expr, attr.name); bound.has_value()) {
                                attr_iv = attr_iv.intersect_with(*bound);
                            }
                        }
                        if (t.guard.has_value()) {
                            if (auto g_bound = parse_guard_domain(*t.guard, attr.name); g_bound.has_value()) {
                                attr_iv = attr_iv.intersect_with(*g_bound);
                            }
                        }
                        for (const std::string& pfx : {std::string("payload."), std::string("cmd."),
                                                       std::string("event."), std::string("ev.")}) {
                            std::string k = pfx + attr.name;
                            next_env[k] = attr_iv;
                            ephemeral_payload_keys.insert(k);
                        }
                        if (!sig_trig->payload_binding.empty()) {
                            std::string k = sig_trig->payload_binding + "." + attr.name;
                            next_env[k] = attr_iv;
                            ephemeral_payload_keys.insert(k);
                        }
                    }
                }
            }

            auto process_action_assignments = [&](const std::optional<ActionSignature>& act_opt) {
                if (!act_opt.has_value())
                    return;
                for (const auto& assign : act_opt->assignments) {
                    apply_assignment(next_env, assign);
                }
            };
            process_action_assignments(t.condition_action);
            process_action_assignments(t.transition_action);

            // Merge into target state environment (excluding ephemeral transition-local payload bindings)
            auto& target_env = state_envs[t.target];
            bool changed = false;
            bool apply_widening = (state_visits[t.target] >= 3);

            for (const auto& [var_name, interval] : next_env) {
                if (ephemeral_payload_keys.count(var_name) > 0) {
                    continue;
                }
                auto it_tgt = target_env.find(var_name);
                if (it_tgt == target_env.end()) {
                    target_env[var_name] = interval;
                    changed = true;
                } else {
                    auto merged =
                        apply_widening ? it_tgt->second.widen_with(interval) : it_tgt->second.join_with(interval);
                    if (merged != it_tgt->second) {
                        it_tgt->second = merged;
                        changed = true;
                    }
                }
            }

            if (changed && in_worklist.count(t.target) == 0) {
                worklist.push(t.target);
                in_worklist.insert(t.target);
            }
        }
    }

    return state_envs;
}

std::vector<EFSMAnalysisFinding> EFSMIntervalAnalyzer::analyze(DiagnosticEngine& diag) {
    std::vector<EFSMAnalysisFinding> findings;

    if (ir_.variables.empty() && ir_.ports.empty()) {
        return findings;
    }

    auto state_envs = compute_state_intervals();

    for (const auto& t : ir_.transitions) {
        auto it_curr = state_envs.find(t.source);
        if (it_curr == state_envs.end()) {
            continue;
        }
        const auto& curr_env = it_curr->second;

        // Check guard satisfiability
        if (t.guard.has_value() && !t.guard->empty() && *t.guard != "else" && *t.guard != "default") {
            std::string g_str = *t.guard;
            for (const auto& gm : ir_.guards) {
                if (gm.name == g_str && gm.raw_expression.has_value()) {
                    g_str = *gm.raw_expression;
                    break;
                }
            }

            for (const auto& [var_name, var_interval] : curr_env) {
                auto guard_interval = parse_guard_domain(g_str, var_name);
                if (guard_interval.has_value()) {
                    auto intersection = var_interval.intersect_with(*guard_interval);
                    if (intersection.is_empty()) {
                        std::string msg = "Guard '" + g_str + "' on transition '" + t.source + " -> " + t.target +
                                          "' is unsatisfiable given variable/port '" + var_name + "' range " +
                                          var_interval.to_string();
                        findings.push_back(
                            {var_name, t.id, t.source, t.target, msg, false, EFSMFindingKind::UnsatisfiableGuard});
                        diag.report(Diagnostic::warning("W_EFSM_UNSATISFIABLE_GUARD", msg));
                    }
                }
            }
        }

        // Check assignments
        auto next_env = curr_env;
        auto check_actions = [&](const std::optional<ActionSignature>& act_opt) {
            if (!act_opt.has_value())
                return;
            for (const auto& assign : act_opt->assignments) {
                apply_assignment(next_env, assign);

                const auto* out_p = ir_.find_port(assign.target.name);
                if (out_p != nullptr && out_p->is_out()) {
                    if (out_p->min_value.has_value() || out_p->max_value.has_value()) {
                        double lo = out_p->min_value.value_or(-std::numeric_limits<double>::infinity());
                        double hi = out_p->max_value.value_or(std::numeric_limits<double>::infinity());
                        Interval port_bound(lo, hi);
                        auto assigned_interval = next_env[assign.target.name];
                        auto intersection = port_bound.intersect_with(assigned_interval);
                        if (intersection.is_empty()) {
                            std::string msg = "Out-port '" + out_p->name + "' contract violation on transition '" +
                                              t.source + " -> " + t.target + "': assigned range " +
                                              assigned_interval.to_string() + " violates contract " +
                                              port_bound.to_string();
                            findings.push_back({out_p->name, t.id, t.source, t.target, msg, true,
                                                EFSMFindingKind::PortRangeViolation});
                            diag.report(Diagnostic::warning("W_PORT_RANGE_VIOLATION", msg));
                        }
                    }
                }

                const auto* var_def = ir_.find_variable(assign.target.name);
                if (var_def != nullptr) {
                    if (var_def->min_value.has_value() || var_def->max_value.has_value()) {
                        double lo = var_def->min_value.value_or(-std::numeric_limits<double>::infinity());
                        double hi = var_def->max_value.value_or(std::numeric_limits<double>::infinity());
                        Interval var_bound(lo, hi);
                        auto assigned_interval = next_env[assign.target.name];
                        auto intersection = var_bound.intersect_with(assigned_interval);
                        if (intersection.is_empty()) {
                            std::string msg = "Register variable '" + var_def->name +
                                              "' contract violation on transition '" + t.source + " -> " + t.target +
                                              "': assigned range " + assigned_interval.to_string() +
                                              " violates contract " + var_bound.to_string();
                            findings.push_back({var_def->name, t.id, t.source, t.target, msg, true,
                                                EFSMFindingKind::VariableRangeViolation});
                            diag.report(Diagnostic::warning("W_VARIABLE_RANGE_VIOLATION", msg));
                        }
                    }
                }
            }
        };
        check_actions(t.condition_action);
        check_actions(t.transition_action);
    }

    return findings;
}

std::string EFSMIntervalAnalyzer::strip_qualifier(const std::string& name) {
    auto pos = name.rfind('.');
    if (pos != std::string::npos) {
        return name.substr(pos + 1);
    }
    return name;
}

std::string EFSMIntervalAnalyzer::clean_number_literal(std::string s) {
    while (!s.empty() && (s.back() == 'f' || s.back() == 'F' || s.back() == 'u' || s.back() == 'U' || s.back() == 'l' ||
                          s.back() == 'L')) {
        s.pop_back();
    }
    return s;
}

std::optional<Interval> EFSMIntervalAnalyzer::parse_guard_domain(std::string_view expr, std::string_view var_name) {
    if (expr.empty() || var_name.empty()) {
        return std::nullopt;
    }

    // 1. Check for top-level disjunctions ('||')
    int paren_depth = 0;
    std::vector<std::string_view> or_branches;
    size_t branch_start = 0;
    for (size_t i = 0; i < expr.size(); ++i) {
        if (expr[i] == '(') {
            paren_depth++;
        } else if (expr[i] == ')') {
            if (paren_depth > 0)
                paren_depth--;
        } else if (paren_depth == 0 && i + 1 < expr.size() && expr[i] == '|' && expr[i + 1] == '|') {
            or_branches.push_back(expr.substr(branch_start, i - branch_start));
            i++;  // skip second '|'
            branch_start = i + 1;
        }
    }
    if (!or_branches.empty()) {
        or_branches.push_back(expr.substr(branch_start));
        std::optional<Interval> combined_hull;
        for (const auto& branch : or_branches) {
            auto b_iv = parse_guard_domain(branch, var_name);
            if (!b_iv.has_value()) {
                // If any disjunctive branch does not restrict var_name, var_name is unrestricted
                return std::nullopt;
            }
            if (!combined_hull.has_value()) {
                combined_hull = *b_iv;
            } else {
                combined_hull = combined_hull->join_with(*b_iv);
            }
        }
        return combined_hull;
    }

    // 2. Conjunctions / multiple atomic constraints on var_name
    std::optional<Interval> combined_iv;
    size_t pos = 0;
    while (pos < expr.size()) {
        size_t found = expr.find(var_name, pos);
        if (found == std::string_view::npos) {
            break;
        }

        bool prefix_ok = false;
        bool is_negated = false;
        if (found == 0) {
            prefix_ok = true;
        } else {
            char before = expr[found - 1];
            if (before == '.') {
                std::string_view prefix_str = expr.substr(0, found - 1);
                auto last_delim = prefix_str.find_last_of(" \t\r\n(");
                std::string_view qual =
                    (last_delim == std::string_view::npos) ? prefix_str : prefix_str.substr(last_delim + 1);
                if (qual == "in" || qual == "reg" || qual == "out" || qual == "cmd" || qual == "event" ||
                    qual == "payload") {
                    prefix_ok = true;
                }
            } else if (!std::isalnum(static_cast<unsigned char>(before)) && before != '_') {
                prefix_ok = true;
            }

            // Check if preceded by '!' outside prefix
            if (prefix_ok) {
                size_t p_scan = found;
                while (p_scan > 0 &&
                       (expr[p_scan - 1] == ' ' || expr[p_scan - 1] == '\t' || expr[p_scan - 1] == '(' ||
                        expr[p_scan - 1] == '.' || std::isalpha(static_cast<unsigned char>(expr[p_scan - 1])))) {
                    if (expr[p_scan - 1] == '!') {
                        is_negated = true;
                        break;
                    }
                    p_scan--;
                }
            }
        }

        size_t after_var = found + var_name.size();
        bool suffix_ok = false;
        if (after_var >= expr.size()) {
            suffix_ok = true;
        } else {
            char after = expr[after_var];
            if (!std::isalnum(static_cast<unsigned char>(after)) && after != '_') {
                suffix_ok = true;
            }
        }

        if (prefix_ok && suffix_ok) {
            std::string_view rem = expr.substr(after_var);
            while (!rem.empty() && (rem.front() == ' ' || rem.front() == '\t')) {
                rem.remove_prefix(1);
            }

            enum class Op { Gte, Gt, Lte, Lt, Eq, Ne, Unknown };
            Op op = Op::Unknown;
            if (rem.size() >= 2 && rem[0] == '>' && rem[1] == '=') {
                op = Op::Gte;
                rem.remove_prefix(2);
            } else if (!rem.empty() && rem[0] == '>') {
                op = Op::Gt;
                rem.remove_prefix(1);
            } else if (rem.size() >= 2 && rem[0] == '<' && rem[1] == '=') {
                op = Op::Lte;
                rem.remove_prefix(2);
            } else if (!rem.empty() && rem[0] == '<') {
                op = Op::Lt;
                rem.remove_prefix(1);
            } else if (rem.size() >= 2 && rem[0] == '=' && rem[1] == '=') {
                op = Op::Eq;
                rem.remove_prefix(2);
            } else if (rem.size() >= 2 && rem[0] == '!' && rem[1] == '=') {
                op = Op::Ne;
                rem.remove_prefix(2);
            }

            if (is_negated && op != Op::Unknown) {
                if (op == Op::Gte)
                    op = Op::Lt;
                else if (op == Op::Gt)
                    op = Op::Lte;
                else if (op == Op::Lte)
                    op = Op::Gt;
                else if (op == Op::Lt)
                    op = Op::Gte;
                else if (op == Op::Eq)
                    op = Op::Ne;
                else if (op == Op::Ne)
                    op = Op::Eq;
            }

            if (op != Op::Unknown) {
                while (!rem.empty() && (rem.front() == ' ' || rem.front() == '\t')) {
                    rem.remove_prefix(1);
                }
                size_t num_len = 0;
                if (!rem.empty() && (rem[0] == '+' || rem[0] == '-')) {
                    num_len++;
                }
                bool has_digits = false;
                while (num_len < rem.size() &&
                       (std::isdigit(static_cast<unsigned char>(rem[num_len])) || rem[num_len] == '.')) {
                    if (std::isdigit(static_cast<unsigned char>(rem[num_len]))) {
                        has_digits = true;
                    }
                    num_len++;
                }
                if (has_digits) {
                    std::string num_str(rem.substr(0, num_len));
                    try {
                        double c = std::stod(num_str);
                        Interval clause_iv;
                        bool valid_clause = true;
                        switch (op) {
                            case Op::Gte:
                                clause_iv = Interval(c, std::numeric_limits<double>::infinity());
                                break;
                            case Op::Gt:
                                clause_iv = Interval(c + 1e-6, std::numeric_limits<double>::infinity());
                                break;
                            case Op::Lte:
                                clause_iv = Interval(-std::numeric_limits<double>::infinity(), c);
                                break;
                            case Op::Lt:
                                clause_iv = Interval(-std::numeric_limits<double>::infinity(), c - 1e-6);
                                break;
                            case Op::Eq:
                                clause_iv = Interval(c, c);
                                break;
                            default:
                                valid_clause = false;
                                break;
                        }
                        if (valid_clause) {
                            if (!combined_iv.has_value()) {
                                combined_iv = clause_iv;
                            } else {
                                combined_iv = combined_iv->intersect_with(clause_iv);
                            }
                        }
                    } catch (...) {
                    }
                }
            }
        }

        pos = found + 1;
    }

    return combined_iv;
}

void EFSMIntervalAnalyzer::apply_assignment(std::unordered_map<std::string, Interval>& env,
                                            const ActionAssignment& assign) {
    const std::string var = strip_qualifier(assign.target.name);
    std::string expr = assign.expression;

    auto update_target = [&](const Interval& rhs_iv) {
        if (assign.op == AssignmentOp::Assign) {
            env[var] = rhs_iv;
        } else if (assign.op == AssignmentOp::AddAssign) {
            env[var] = env[var].add(rhs_iv);
        } else if (assign.op == AssignmentOp::SubAssign) {
            env[var] = env[var].sub(rhs_iv);
        } else if (assign.op == AssignmentOp::MulAssign) {
            env[var] = env[var].mul(rhs_iv);
        } else if (assign.op == AssignmentOp::DivAssign) {
            if (!rhs_iv.is_empty() && std::abs(rhs_iv.lo - rhs_iv.hi) < 1e-6 && rhs_iv.lo != 0.0) {
                env[var] = env[var].div(rhs_iv.lo);
            } else {
                env[var] = Interval();
            }
        }
    };

    if (expr == "true" || expr == "true;") {
        update_target(Interval(1.0, 1.0));
        return;
    }
    if (expr == "false" || expr == "false;") {
        update_target(Interval(0.0, 0.0));
        return;
    }

    // Pure numeric literal check (must consume the entire string)
    try {
        size_t idx = 0;
        std::string cleaned = clean_number_literal(expr);
        if (!cleaned.empty()) {
            double c = std::stod(cleaned, &idx);
            while (idx < cleaned.size() && (cleaned[idx] == ' ' || cleaned[idx] == '\t' || cleaned[idx] == ';')) {
                idx++;
            }
            if (idx == cleaned.size()) {
                update_target(Interval(c, c));
                return;
            }
        }
    } catch (...) {
    }

    if (auto it_raw = env.find(expr); it_raw != env.end()) {
        update_target(it_raw->second);
        return;
    }

    std::string clean_expr = strip_qualifier(expr);
    auto it_direct = env.find(clean_expr);
    if (it_direct != env.end()) {
        update_target(it_direct->second);
        return;
    }

    static const std::regex add_re(
        R"((?:(?:in|reg|out)\.)?([a-zA-Z0-9_]+)\s*\+\s*([+-]?\d+(?:\.\d+)?[fFuUlL]*)|([+-]?\d+(?:\.\d+)?[fFuUlL]*)\s*\+\s*(?:(?:in|reg|out)\.)?([a-zA-Z0-9_]+))");
    std::smatch match;
    if (std::regex_search(expr, match, add_re)) {
        std::string matched_var = match[1].matched ? match[1].str() : match[4].str();
        std::string num_str = match[1].matched ? match[2].str() : match[3].str();
        double k = std::stod(clean_number_literal(num_str));
        auto it = env.find(matched_var);
        if (it != env.end()) {
            update_target(it->second.add(k));
            return;
        }
    }

    std::regex sub_re(R"((?:(?:in|reg|out)\.)?([a-zA-Z0-9_]+)\s*-\s*([+-]?\d+(?:\.\d+)?[fFuUlL]*))");
    if (std::regex_search(expr, match, sub_re)) {
        std::string matched_var = match[1].str();
        double k = std::stod(clean_number_literal(match[2].str()));
        auto it = env.find(matched_var);
        if (it != env.end()) {
            update_target(it->second.sub(k));
            return;
        }
    }

    static const std::regex mul_re(
        R"((?:(?:in|reg|out)\.)?([a-zA-Z0-9_]+)\s*\*\s*([+-]?\d+(?:\.\d+)?[fFuUlL]*)|([+-]?\d+(?:\.\d+)?[fFuUlL]*)\s*\*\s*(?:(?:in|reg|out)\.)?([a-zA-Z0-9_]+))");
    if (std::regex_search(expr, match, mul_re)) {
        std::string matched_var = match[1].matched ? match[1].str() : match[4].str();
        std::string num_str = match[1].matched ? match[2].str() : match[3].str();
        double k = std::stod(clean_number_literal(num_str));
        auto it = env.find(matched_var);
        if (it != env.end()) {
            update_target(it->second.mul(k));
            return;
        }
    }

    std::regex div_re(R"((?:(?:in|reg|out)\.)?([a-zA-Z0-9_]+)\s*/\s*([+-]?\d+(?:\.\d+)?[fFuUlL]*))");
    if (std::regex_search(expr, match, div_re)) {
        std::string matched_var = match[1].str();
        double k = std::stod(clean_number_literal(match[2].str()));
        auto it = env.find(matched_var);
        if (it != env.end()) {
            update_target(it->second.div(k));
            return;
        }
    }

    static const std::regex bin_var_re(
        R"((?:(?:in|reg|out)\.)?([a-zA-Z0-9_]+)\s*([\+\-\*])\s*(?:(?:in|reg|out)\.)?([a-zA-Z0-9_]+))");
    if (std::regex_search(expr, match, bin_var_re)) {
        std::string v1 = match[1].str();
        std::string op = match[2].str();
        std::string v2 = match[3].str();
        auto it1 = env.find(v1);
        auto it2 = env.find(v2);
        if (it1 != env.end() && it2 != env.end()) {
            if (op == "+")
                update_target(it1->second.add(it2->second));
            else if (op == "-")
                update_target(it1->second.sub(it2->second));
            else if (op == "*")
                update_target(it1->second.mul(it2->second));
            return;
        }
    }

    update_target(Interval());
}

}  // namespace fsm::middleend::analysis

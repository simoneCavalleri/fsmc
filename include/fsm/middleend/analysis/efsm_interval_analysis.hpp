#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <queue>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::analysis {

/**
 * @brief Numeric Interval representation for Abstract Interpretation over EFSM Data Paths.
 */
struct Interval {
    double lo{-std::numeric_limits<double>::infinity()};
    double hi{std::numeric_limits<double>::infinity()};

    constexpr Interval() = default;
    constexpr Interval(double l, double h) : lo(l), hi(h) {}
    constexpr explicit Interval(double val) : lo(val), hi(val) {}

    [[nodiscard]] bool is_empty() const noexcept { return lo > hi; }

    [[nodiscard]] bool contains(double val) const noexcept { return val >= lo && val <= hi; }

    [[nodiscard]] Interval intersect_with(const Interval& other) const noexcept {
        return Interval(std::max(lo, other.lo), std::min(hi, other.hi));
    }

    [[nodiscard]] Interval join_with(const Interval& other) const noexcept {
        if (is_empty())
            return other;
        if (other.is_empty())
            return *this;
        return Interval(std::min(lo, other.lo), std::max(hi, other.hi));
    }

    [[nodiscard]] Interval add(double k) const noexcept { return Interval(lo + k, hi + k); }

    [[nodiscard]] Interval sub(double k) const noexcept { return Interval(lo - k, hi - k); }

    [[nodiscard]] std::string to_string() const {
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

    bool operator==(const Interval& other) const noexcept {
        return (is_empty() && other.is_empty()) || (std::abs(lo - other.lo) < 1e-9 && std::abs(hi - other.hi) < 1e-9);
    }

    bool operator!=(const Interval& other) const noexcept { return !(*this == other); }
};

struct EFSMAnalysisFinding {
    std::string variable_name;
    std::string transition_id;
    std::string source_state;
    std::string target_state;
    std::string message;
    bool is_error{false};
};

/**
 * @brief Formal Verification: EFSM Data Path Abstract Interpreter.
 *
 * Propagates numeric variable intervals across the statechart's reachable paths
 * to detect:
 * 1. Statically unsatisfiable guard conditions (Dead Branches).
 * 2. Potential out-of-range assignments violating variable constraints.
 */
class EFSMIntervalAnalyzer {
  public:
    explicit EFSMIntervalAnalyzer(const FsmIr& ir) : ir_(ir) {}

    std::vector<EFSMAnalysisFinding> analyze(DiagnosticEngine& diag) {
        std::vector<EFSMAnalysisFinding> findings;

        if (ir_.variables.empty() && ir_.ports.empty()) {
            return findings;
        }

        // 1. Initialize environment for initial state
        std::unordered_map<std::string, std::unordered_map<std::string, Interval>> state_envs;
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
            if (port.is_in()) {
                if (port.min_value.has_value() || port.max_value.has_value()) {
                    double lo = port.min_value.value_or(-std::numeric_limits<double>::infinity());
                    double hi = port.max_value.value_or(std::numeric_limits<double>::infinity());
                    init_env[port.name] = Interval(lo, hi);
                } else {
                    init_env[port.name] = Interval();
                }
            }
        }

        std::string root =
            ir_.initial_state.empty() ? (ir_.states.empty() ? "" : ir_.states.front().name) : ir_.initial_state;
        if (root.empty()) {
            return findings;
        }

        state_envs[root] = init_env;

        // 2. Fixed-point iteration with worklist
        std::queue<std::string> worklist;
        std::unordered_set<std::string> in_worklist;
        worklist.push(root);
        in_worklist.insert(root);

        std::size_t iterations = 0;
        constexpr std::size_t kMaxIterations = 200;

        while (!worklist.empty() && iterations++ < kMaxIterations) {
            std::string curr_state = worklist.front();
            worklist.pop();
            in_worklist.erase(curr_state);

            const auto curr_env = state_envs[curr_state];

            // Inspect all outgoing transitions from curr_state
            for (const auto& t : ir_.transitions) {
                if (t.source != curr_state) {
                    continue;
                }

                // Check guard satisfiability
                if (t.guard.has_value() && !t.guard->empty() && *t.guard != "else" && *t.guard != "default") {
                    std::string g_str = *t.guard;
                    // Find if any guard references a model guard with raw/cpp expression
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
                                std::string msg = "Guard '" + g_str + "' on transition '" + t.source + " -> " +
                                                  t.target + "' is unsatisfiable given variable/port '" + var_name +
                                                  "' range " + var_interval.to_string();
                                findings.push_back({var_name, t.id, t.source, t.target, msg, false});
                                diag.report(Diagnostic::warning("W_EFSM_UNSATISFIABLE_GUARD", msg));
                            }
                        }
                    }
                }

                // Propagate variable and port assignments across the transition
                auto next_env = curr_env;
                if (t.action_sig.has_value()) {
                    for (const auto& assign : t.action_sig->assignments) {
                        apply_assignment(next_env, assign);

                        // Check out-port domain contracts
                        const auto* out_p = ir_.find_port(assign.target_variable);
                        if (out_p != nullptr && out_p->is_out()) {
                            if (out_p->min_value.has_value() || out_p->max_value.has_value()) {
                                double lo = out_p->min_value.value_or(-std::numeric_limits<double>::infinity());
                                double hi = out_p->max_value.value_or(std::numeric_limits<double>::infinity());
                                Interval port_bound(lo, hi);
                                auto assigned_interval = next_env[assign.target_variable];
                                auto intersection = port_bound.intersect_with(assigned_interval);
                                if (intersection.is_empty()) {
                                    std::string msg = "Out-port '" + out_p->name +
                                                      "' contract violation on transition '" + t.source + " -> " +
                                                      t.target + "': assigned range " + assigned_interval.to_string() +
                                                      " violates contract " + port_bound.to_string();
                                    findings.push_back({out_p->name, t.id, t.source, t.target, msg, true});
                                    diag.report(Diagnostic::warning("W_PORT_RANGE_VIOLATION", msg));
                                }
                            }
                        }

                        // Check register variable domain contracts
                        const auto* var_def = ir_.find_variable(assign.target_variable);
                        if (var_def != nullptr) {
                            if (var_def->min_value.has_value() || var_def->max_value.has_value()) {
                                double lo = var_def->min_value.value_or(-std::numeric_limits<double>::infinity());
                                double hi = var_def->max_value.value_or(std::numeric_limits<double>::infinity());
                                Interval var_bound(lo, hi);
                                auto assigned_interval = next_env[assign.target_variable];
                                auto intersection = var_bound.intersect_with(assigned_interval);
                                if (intersection.is_empty()) {
                                    std::string msg = "Register variable '" + var_def->name +
                                                      "' contract violation on transition '" + t.source + " -> " +
                                                      t.target + "': assigned range " + assigned_interval.to_string() +
                                                      " violates contract " + var_bound.to_string();
                                    findings.push_back({var_def->name, t.id, t.source, t.target, msg, true});
                                    diag.report(Diagnostic::warning("W_VARIABLE_RANGE_VIOLATION", msg));
                                }
                            }
                        }
                    }
                }

                // Merge into target state environment
                auto& target_env = state_envs[t.target];
                bool changed = false;

                for (const auto& [var_name, interval] : next_env) {
                    auto it_tgt = target_env.find(var_name);
                    if (it_tgt == target_env.end()) {
                        target_env[var_name] = interval;
                        changed = true;
                    } else {
                        auto merged = it_tgt->second.join_with(interval);
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

        return findings;
    }

    static std::string strip_qualifier(const std::string& name) {
        auto pos = name.rfind('.');
        if (pos != std::string::npos) {
            return name.substr(pos + 1);
        }
        return name;
    }

    static std::string clean_number_literal(std::string s) {
        // Strip trailing f, F, u, U, l, L
        while (!s.empty() && (s.back() == 'f' || s.back() == 'F' || s.back() == 'u' || s.back() == 'U' ||
                              s.back() == 'l' || s.back() == 'L')) {
            s.pop_back();
        }
        return s;
    }

    static std::optional<Interval> parse_guard_domain(std::string_view expr, std::string_view var_name) {
        // Scan for var_name in expr, optionally preceded by in., reg., out., cmd., event., payload.
        size_t pos = 0;
        while (pos < expr.size()) {
            size_t found = expr.find(var_name, pos);
            if (found == std::string_view::npos) {
                return std::nullopt;
            }

            // Verify prefix: either at start, or preceded by '.', whitespace, or non-alnum
            bool prefix_ok = false;
            if (found == 0) {
                prefix_ok = true;
            } else {
                char before = expr[found - 1];
                if (before == '.') {
                    // Check if qualifier is in/reg/out/cmd/event/payload
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
            }

            // Verify suffix: character after var_name must not be alnum or '_'
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
                // Found matching variable! Look for relational operator following var_name
                std::string_view rem = expr.substr(after_var);
                while (!rem.empty() && (rem.front() == ' ' || rem.front() == '\t')) {
                    rem.remove_prefix(1);
                }

                enum class Op { Gte, Gt, Lte, Lt, Eq, Unknown };
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
                }

                if (op != Op::Unknown) {
                    while (!rem.empty() && (rem.front() == ' ' || rem.front() == '\t')) {
                        rem.remove_prefix(1);
                    }
                    // Parse number
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
                            switch (op) {
                                case Op::Gte:
                                    return Interval(c, std::numeric_limits<double>::infinity());
                                case Op::Gt:
                                    return Interval(c + 1e-6, std::numeric_limits<double>::infinity());
                                case Op::Lte:
                                    return Interval(-std::numeric_limits<double>::infinity(), c);
                                case Op::Lt:
                                    return Interval(-std::numeric_limits<double>::infinity(), c - 1e-6);
                                case Op::Eq:
                                    return Interval(c, c);
                                default:
                                    break;
                            }
                        } catch (...) {
                            return std::nullopt;
                        }
                    }
                }
            }

            pos = found + 1;
        }

        return std::nullopt;
    }

    static void apply_assignment(std::unordered_map<std::string, Interval>& env, const ActionAssignment& assign) {
        const std::string var = strip_qualifier(assign.target_variable);
        std::string expr = assign.expression;

        // Boolean literal assignment
        if (expr == "true" || expr == "true;") {
            env[var] = Interval(1.0, 1.0);
            return;
        }
        if (expr == "false" || expr == "false;") {
            env[var] = Interval(0.0, 0.0);
            return;
        }

        // 1. var = c (constant numeric literal)
        try {
            double c = std::stod(clean_number_literal(expr));
            env[var] = Interval(c, c);
            return;
        } catch (...) {
        }

        // 2. Direct variable copy: var = other_var (with optional qualifier in./reg.)
        std::string clean_expr = strip_qualifier(expr);
        auto it_direct = env.find(clean_expr);
        if (it_direct != env.end()) {
            env[var] = it_direct->second;
            return;
        }

        // 3. var += k / var = var + k
        static const std::regex add_re(
            R"((?:(?:in|reg|out)\.)?([a-zA-Z0-9_]+)\s*\+\s*([+-]?\d+(?:\.\d+)?[fFuUlL]*)|([+-]?\d+(?:\.\d+)?[fFuUlL]*)\s*\+\s*(?:(?:in|reg|out)\.)?([a-zA-Z0-9_]+))");
        std::smatch match;
        if (std::regex_search(expr, match, add_re)) {
            std::string matched_var = match[1].matched ? match[1].str() : match[4].str();
            if (matched_var == var) {
                std::string num_str = match[1].matched ? match[2].str() : match[3].str();
                double k = std::stod(clean_number_literal(num_str));
                env[var] = env[var].add(k);
                return;
            }
        }

        // 4. var -= k / var = var - k
        std::regex sub_re(R"((?:(?:in|reg|out)\.)?)" + var + R"(\s*-\s*([+-]?\d+(?:\.\d+)?[fFuUlL]*))");
        if (std::regex_search(expr, match, sub_re)) {
            double k = std::stod(clean_number_literal(match[1].str()));
            env[var] = env[var].sub(k);
            return;
        }

        // Fallback: unconstrained interval
        env[var] = Interval();
    }

    const FsmIr& ir_;
};

}  // namespace fsm::middleend::analysis

namespace fsm::middleend {
using analysis::Interval;
using analysis::EFSMIntervalAnalyzer;
}  // namespace fsm::middleend

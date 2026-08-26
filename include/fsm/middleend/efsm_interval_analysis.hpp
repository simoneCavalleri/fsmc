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

namespace fsm::codegen {

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

        if (ir_.variables.empty()) {
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
                if (t.source != curr_state && t.source_id != curr_state) {
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

                    for (const auto& var : ir_.variables) {
                        auto it_var = curr_env.find(var.name);
                        if (it_var == curr_env.end())
                            continue;

                        auto guard_interval = parse_guard_domain(g_str, var.name);
                        if (guard_interval.has_value()) {
                            auto intersection = it_var->second.intersect_with(*guard_interval);
                            if (intersection.is_empty()) {
                                std::string msg = "Guard '" + g_str + "' on transition '" + t.source + " -> " +
                                                  t.target + "' is unsatisfiable given variable '" + var.name +
                                                  "' range " + it_var->second.to_string();
                                findings.push_back({var.name, t.id, t.source, t.target, msg, false});
                                diag.report(Diagnostic::warning("W_EFSM_UNSATISFIABLE_GUARD", msg));
                            }
                        }
                    }
                }

                // Propagate variable assignments across the transition
                auto next_env = curr_env;
                if (t.action_sig.has_value()) {
                    for (const auto& assign : t.action_sig->assignments) {
                        apply_assignment(next_env, assign);
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

  private:
    static std::optional<Interval> parse_guard_domain(const std::string& expr, const std::string& var_name) {
        // Match: var > c, var >= c, var < c, var <= c, var == c
        std::regex gt_re(var_name + R"(\s*>\s*([+-]?\d+(?:\.\d+)?))");
        std::regex gte_re(var_name + R"(\s*>=\s*([+-]?\d+(?:\.\d+)?))");
        std::regex lt_re(var_name + R"(\s*<\s*([+-]?\d+(?:\.\d+)?))");
        std::regex lte_re(var_name + R"(\s*<=\s*([+-]?\d+(?:\.\d+)?))");
        std::regex eq_re(var_name + R"(\s*==\s*([+-]?\d+(?:\.\d+)?))");

        std::smatch match;
        if (std::regex_search(expr, match, gte_re)) {
            double c = std::stod(match[1].str());
            return Interval(c, std::numeric_limits<double>::infinity());
        }
        if (std::regex_search(expr, match, gt_re)) {
            double c = std::stod(match[1].str());
            return Interval(c + 1e-6, std::numeric_limits<double>::infinity());
        }
        if (std::regex_search(expr, match, lte_re)) {
            double c = std::stod(match[1].str());
            return Interval(-std::numeric_limits<double>::infinity(), c);
        }
        if (std::regex_search(expr, match, lt_re)) {
            double c = std::stod(match[1].str());
            return Interval(-std::numeric_limits<double>::infinity(), c - 1e-6);
        }
        if (std::regex_search(expr, match, eq_re)) {
            double c = std::stod(match[1].str());
            return Interval(c, c);
        }

        return std::nullopt;
    }

    static void apply_assignment(std::unordered_map<std::string, Interval>& env, const ActionAssignment& assign) {
        const std::string& var = assign.target_variable;
        const std::string& expr = assign.expression;

        // 1. var = c (constant)
        try {
            double c = std::stod(expr);
            env[var] = Interval(c, c);
            return;
        } catch (...) {
        }

        // 2. var += k / var = var + k
        std::regex add_re(R"((?:)" + var + R"(\s*\+\s*([+-]?\d+(?:\.\d+)?)|([+-]?\d+(?:\.\d+)?)\s*\+\s*)" + var +
                          R"())");
        std::smatch match;
        if (std::regex_search(expr, match, add_re)) {
            std::string num_str = match[1].matched ? match[1].str() : match[2].str();
            double k = std::stod(num_str);
            env[var] = env[var].add(k);
            return;
        }

        // 3. var -= k / var = var - k
        std::regex sub_re(var + R"(\s*-\s*([+-]?\d+(?:\.\d+)?))");
        if (std::regex_search(expr, match, sub_re)) {
            double k = std::stod(match[1].str());
            env[var] = env[var].sub(k);
            return;
        }

        // Fallback: unconstrained interval
        env[var] = Interval();
    }

    const FsmIr& ir_;
};

}  // namespace fsm::codegen

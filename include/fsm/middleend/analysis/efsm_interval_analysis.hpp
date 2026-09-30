/**
 * @file efsm_interval_analysis.hpp
 * @brief Abstract interpretation over EFSM data paths and numeric variable intervals.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/action.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::analysis {

using diagnostic::DiagnosticEngine;
using ir::ActionAssignment;
using ir::FsmIr;

/**
 * @struct Interval
 * @brief Numeric Interval representation for Abstract Interpretation over EFSM Data Paths.
 */
struct Interval {
    double lo{-std::numeric_limits<double>::infinity()};  ///< Lower bound
    double hi{std::numeric_limits<double>::infinity()};   ///< Upper bound

    constexpr Interval() = default;
    constexpr Interval(double l, double h) : lo(l), hi(h) {}
    constexpr explicit Interval(double val) : lo(val), hi(val) {}

    [[nodiscard]] constexpr bool is_empty() const noexcept { return lo > hi; }

    [[nodiscard]] constexpr bool contains(double val) const noexcept { return val >= lo && val <= hi; }

    [[nodiscard]] constexpr Interval intersect_with(const Interval& other) const noexcept {
        return Interval((std::max)(lo, other.lo), (std::min)(hi, other.hi));
    }

    [[nodiscard]] constexpr Interval join_with(const Interval& other) const noexcept {
        if (is_empty())
            return other;
        if (other.is_empty())
            return *this;
        return Interval((std::min)(lo, other.lo), (std::max)(hi, other.hi));
    }

    [[nodiscard]] constexpr Interval add(double k) const noexcept { return Interval(lo + k, hi + k); }

    [[nodiscard]] constexpr Interval add(const Interval& other) const noexcept {
        if (is_empty() || other.is_empty()) return Interval(1, 0);
        return Interval(lo + other.lo, hi + other.hi);
    }

    [[nodiscard]] constexpr Interval sub(double k) const noexcept { return Interval(lo - k, hi - k); }

    [[nodiscard]] constexpr Interval sub(const Interval& other) const noexcept {
        if (is_empty() || other.is_empty()) return Interval(1, 0);
        return Interval(lo - other.hi, hi - other.lo);
    }

    [[nodiscard]] constexpr Interval mul(double k) const noexcept {
        if (is_empty()) return *this;
        if (k >= 0) return Interval(lo * k, hi * k);
        return Interval(hi * k, lo * k);
    }

    [[nodiscard]] constexpr Interval mul(const Interval& other) const noexcept {
        if (is_empty() || other.is_empty()) return Interval(1, 0);
        double p1 = lo * other.lo;
        double p2 = lo * other.hi;
        double p3 = hi * other.lo;
        double p4 = hi * other.hi;
        double min_p = (std::min)({p1, p2, p3, p4});
        double max_p = (std::max)({p1, p2, p3, p4});
        return Interval(min_p, max_p);
    }

    [[nodiscard]] constexpr Interval div(double k) const noexcept {
        if (is_empty() || k == 0.0) return Interval();
        if (k > 0) return Interval(lo / k, hi / k);
        return Interval(hi / k, lo / k);
    }

    [[nodiscard]] constexpr Interval widen_with(const Interval& other) const noexcept {
        if (is_empty()) return other;
        if (other.is_empty()) return *this;
        double new_lo = (other.lo < lo) ? -std::numeric_limits<double>::infinity() : lo;
        double new_hi = (other.hi > hi) ? std::numeric_limits<double>::infinity() : hi;
        return Interval(new_lo, new_hi);
    }

    [[nodiscard]] std::string to_string() const;

    bool operator==(const Interval& other) const noexcept {
        return (is_empty() && other.is_empty()) || (std::abs(lo - other.lo) < 1e-9 && std::abs(hi - other.hi) < 1e-9);
    }

    bool operator!=(const Interval& other) const noexcept { return !(*this == other); }
};

/**
 * @struct EFSMAnalysisFinding
 * @brief Diagnostic finding produced by data-path abstract interpretation.
 */
struct EFSMAnalysisFinding {
    std::string variable_name;  ///< Affected variable identifier
    std::string transition_id;  ///< ID of transition triggering the finding
    std::string source_state;   ///< Originating state
    std::string target_state;   ///< Destination state
    std::string message;        ///< Descriptive finding text
    bool is_error{false};       ///< True if violation is fatal, false if warning
};

/**
 * @class EFSMIntervalAnalyzer
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

    /**
     * @brief Computes fixed-point variable intervals and identifies dead transitions or overflows.
     */
    std::vector<EFSMAnalysisFinding> analyze(DiagnosticEngine& diag);

    /**
     * @brief Computes fixed-point variable intervals for each reachable state in the EFSM.
     */
    std::unordered_map<std::string, std::unordered_map<std::string, Interval>> compute_state_intervals();

    static std::string strip_qualifier(const std::string& name);
    static std::string clean_number_literal(std::string s);
    static std::optional<Interval> parse_guard_domain(std::string_view expr, std::string_view var_name);
    static void apply_assignment(std::unordered_map<std::string, Interval>& env, const ActionAssignment& assign);

  private:
    const FsmIr& ir_;
};

}  // namespace fsm::middleend::analysis

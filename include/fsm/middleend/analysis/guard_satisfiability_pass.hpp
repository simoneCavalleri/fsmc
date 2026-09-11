/**
 * @file guard_satisfiability_pass.hpp
 * @brief Mutual exclusivity and satisfiability analysis for transition guards pass.
 */

#pragma once

#include <string>
#include <unordered_map>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"

namespace fsm::middleend::analysis {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class GuardSatisfiabilityPass
 * @brief In-process Middle-End Analysis Pass: Evaluates guard mutual exclusivity and satisfiability.
 *
 * 1. Groups transitions sharing the same (source, event) trigger.
 * 2. Parses guard expressions to extract numeric/interval constraints on variables.
 * 3. Detects overlapping guards (non-deterministic ambiguity, warning W0301).
 * 4. Detects unsatisfiable / dead guards (warning W0302).
 */
class GuardSatisfiabilityPass {
  public:
    [[nodiscard]] static std::string name() { return "GuardSatisfiabilityAnalysis"; }
    [[nodiscard]] static std::string description() {
        return "Analyzes guard expressions for satisfiability and mutual exclusivity without external solver "
               "dependencies";
    }

    /**
     * @brief Performs satisfiability and exclusivity checks across all transition guards.
     * @param ir Target FsmIr to analyze.
     * @param diag Diagnostic engine for reporting conflicts.
     * @return True if analysis completed without fatal errors, false otherwise.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);

  private:
    static std::string clean_number(std::string s);
    static std::unordered_map<std::string, Interval> extract_guard_constraints(const std::string& guard_str);
};

}  // namespace fsm::middleend::analysis

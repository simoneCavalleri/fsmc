/**
 * @file priority_conflict_pass.hpp
 * @brief Hierarchical and peer priority conflict analysis pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::analysis {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class PriorityConflictPass
 * @brief Middle-End Verification Pass: Detects Hierarchical and Preemption Priority Inconsistencies.
 *
 * Verifies consistency with the model's concurrency priority policy (e.g. OuterFirst vs InnerFirst):
 * 1. Checks whether explicit transition priority numbers contradict the hierarchical precedence rule.
 * 2. Checks whether conflicting transitions responding to identical triggers lack explicit priorities.
 * Reports diagnostic warnings (W0402) and errors (E0402) ensuring deterministic semantics.
 */
class PriorityConflictPass {
  public:
    [[nodiscard]] static std::string name() { return "PriorityConflictAnalysis"; }
    [[nodiscard]] static std::string description() {
        return "Validates deterministic transition preemption and hierarchical priority compliance";
    }

    /**
     * @brief Executes priority conflict verification across all states.
     * @param ir Target FsmIr to verify.
     * @param diag Diagnostic reporting engine.
     * @return True if no fatal priority contradictions exist.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::analysis

/**
 * @file determinism_enforcement_pass.hpp
 * @brief Determinism verification and transition priority canonicalization pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class DeterminismEnforcementPass
 * @brief Target-Agnostic Middle-End Pass: Enforces transition determinism and canonical priority ordering.
 */
class DeterminismEnforcementPass {
  public:
    [[nodiscard]] static std::string name() { return "DeterminismEnforcement"; }
    [[nodiscard]] static std::string description() {
        return "Enforces deterministic event dispatching and applies priority-based canonical ordering";
    }

    /**
     * @brief Sorts and validates outgoing transitions from each state according to priority contracts.
     * @param ir Target FsmIr to order.
     * @param diag Diagnostic engine for reporting ambiguity.
     * @return True on success, false on fatal determinism conflict.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

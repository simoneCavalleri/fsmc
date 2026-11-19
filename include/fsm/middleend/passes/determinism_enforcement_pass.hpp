/**
 * @file determinism_enforcement_pass.hpp
 * @brief Determinism verification and transition priority canonicalization pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_interface.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class DeterminismEnforcementPass
 * @brief Target-Agnostic Middle-End Pass: Enforces transition determinism and canonical priority ordering.
 */
class DeterminismEnforcementPass : public ::fsm::middleend::IPass {
  public:
/**
     * @brief Sorts and validates outgoing transitions from each state according to priority contracts.
     * @param ir Target FsmIr to order.
     * @param diag Diagnostic engine for reporting ambiguity.
     * @return True on success, false on fatal determinism conflict.
     */
    [[nodiscard]] std::string name() const override { return "determinism-enforcement"; }
    [[nodiscard]] std::string description() const override { return "Verifies transition determinism and detects overlapping nondeterministic triggers."; }
    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

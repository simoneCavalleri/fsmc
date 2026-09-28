/**
 * @file orthogonal_interference_pass.hpp
 * @brief Static data-race and interference analysis pass for orthogonal regions.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class OrthogonalInterferencePass
 * @brief Target-Agnostic Middle-End Pass: Static concurrency & data-race analysis for parallel (AND) orthogonal
 * regions.
 */
class OrthogonalInterferencePass {
  public:
    [[nodiscard]] static std::string name() { return "OrthogonalInterference"; }
    [[nodiscard]] static std::string description() {
        return "Performs static data-race analysis and interference detection across concurrent orthogonal regions";
    }

    /**
     * @brief Detects conflicting concurrent variable read/write actions across orthogonal regions.
     * @param ir Read-only FsmIr model to analyze.
     * @param diag Diagnostic reporting engine for race condition warnings.
     * @return True if model has no fatal race conflicts, false otherwise.
     */
    bool run(const FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

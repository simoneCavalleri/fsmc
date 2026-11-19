/**
 * @file orthogonal_interference_pass.hpp
 * @brief Static data-race and interference analysis pass for orthogonal regions.
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
 * @class OrthogonalInterferencePass
 * @brief Target-Agnostic Middle-End Pass: Static concurrency & data-race analysis for parallel (AND) orthogonal
 * regions.
 */
class OrthogonalInterferencePass : public ::fsm::middleend::IPass {
  public:
/**
     * @brief Detects conflicting concurrent variable read/write actions across orthogonal regions.
     * @param ir Read-only FsmIr model to analyze.
     * @param diag Diagnostic reporting engine for race condition warnings.
     * @return True if model has no fatal race conflicts, false otherwise.
     */
    [[nodiscard]] std::string name() const override { return "orthogonal-interference"; }
    [[nodiscard]] std::string description() const override { return "Detects write-write and read-write data races across parallel orthogonal regions."; }
    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }
    bool run(const FsmIr& ir, DiagnosticEngine& diag);
    bool run(FsmIr& ir, DiagnosticEngine& diag) override { return run(static_cast<const FsmIr&>(ir), diag); }
};

}  // namespace fsm::middleend::passes

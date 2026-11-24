/**
 * @file wcet_analysis_pass.hpp
 * @brief Worst-Case Execution Time (WCET) and Zeno-cycle detection pass.
 */

#pragma once

#include <cstdint>
#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_interface.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class WcetAnalysisPass
 * @brief Formal Middle-End Pass: Worst-Case Execution Time (WCET) & Zeno-Cycle Analysis.
 */
class WcetAnalysisPass : public ::fsm::middleend::IPass {
  public:
    explicit WcetAnalysisPass(std::size_t max_microstep_threshold = 100) : threshold_(max_microstep_threshold) {}

    [[nodiscard]] bool has_zeno_cycle() const noexcept { return has_zeno_cycle_; }
    [[nodiscard]] std::size_t max_micro_steps() const noexcept { return max_micro_steps_; }

    /**
     * @brief Explores micro-step cascades to detect infinite zero-time loops or bound WCET depth.
     * @param ir Target FsmIr to analyze.
     * @param diag Diagnostic engine for Zeno loop errors.
     * @return True if model is free of Zeno cycles within threshold, false otherwise.
     */
    [[nodiscard]] std::string name() const override { return "wcet-analysis"; }
    [[nodiscard]] std::string description() const override {
        return "Computes static worst-case execution time (micro-step bound) per macro-step.";
    }
    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;

  private:
    std::size_t threshold_{100};
    bool has_zeno_cycle_{false};
    std::size_t max_micro_steps_{0};
};

}  // namespace fsm::middleend::passes

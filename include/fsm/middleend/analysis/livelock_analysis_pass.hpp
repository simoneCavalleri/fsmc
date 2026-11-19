/**
 * @file livelock_analysis_pass.hpp
 * @brief Zero-time cycle and Zeno livelock detection pass.
 */

#pragma once

#include <string>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_interface.hpp"

namespace fsm::middleend::analysis {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class LivelockAnalysisPass
 * @brief Middle-End Formal Verification Pass: Detects Zero-Time Loops and Zeno Livelocks.
 *
 * Traverses the state transition graph searching for cycles consisting entirely of
 * instantaneous micro-transitions (anonymous triggers, 0ms timers, or non-progressing
 * completion events) without physical delay or external event consumption.
 * Reports safety-critical errors (E0401) to prevent infinite non-yielding loops
 * in safety-critical RTOS environments and hardware lockups.
 */
class LivelockAnalysisPass : public ::fsm::middleend::IPass {
  public:
/**
     * @brief Performs cycle detection on instantaneous transitions.
     * @param ir Target FsmIr to verify.
     * @param diag Diagnostic reporting engine.
     * @return True if no zero-time livelocks were found, false if fatal cycles exist.
     */
    [[nodiscard]] std::string name() const override { return "livelock-analysis"; }
    [[nodiscard]] std::string description() const override { return "Detects autonomous zero-event livelock cycles in the state graph."; }
    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::analysis

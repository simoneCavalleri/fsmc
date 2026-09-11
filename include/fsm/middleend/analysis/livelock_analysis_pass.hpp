/**
 * @file livelock_analysis_pass.hpp
 * @brief Zero-time cycle and Zeno livelock detection pass.
 */

#pragma once

#include <string>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

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
class LivelockAnalysisPass {
  public:
    [[nodiscard]] static std::string name() { return "LivelockAnalysis"; }
    [[nodiscard]] static std::string description() {
        return "Detects zero-time Zeno cycles and instantaneous livelocks without progress";
    }

    /**
     * @brief Performs cycle detection on instantaneous transitions.
     * @param ir Target FsmIr to verify.
     * @param diag Diagnostic reporting engine.
     * @return True if no zero-time livelocks were found, false if fatal cycles exist.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::analysis

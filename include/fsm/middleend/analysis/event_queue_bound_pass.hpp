/**
 * @file event_queue_bound_pass.hpp
 * @brief Static event queue bound estimation and burst arrival verification pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::analysis {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class EventQueueBoundPass
 * @brief Middle-End Verification Pass: Formally bounds event queue depth for zero-allocation runtimes.
 *
 * Computes safe upper bounds on event queue capacity under burst arrivals:
 * 1. Counts maximum concurrently deferred events across all state configurations.
 * 2. Analyzes cascade signal emissions (SignalEmitOp) produced within single reaction cycles.
 * 3. Annotates the IR with "static_event_queue_capacity" for fixed-size static ring buffer synthesis.
 */
class EventQueueBoundPass {
  public:
    [[nodiscard]] static std::string name() { return "EventQueueBoundAnalysis"; }
    [[nodiscard]] static std::string description() {
        return "Calculates static upper bounds for event queues to support zero-allocation execution";
    }

    /**
     * @brief Computes event queue upper bounds and annotates the model.
     * @param ir Target FsmIr to inspect and annotate.
     * @param diag Diagnostic reporting engine.
     * @return True on success.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::analysis

/**
 * @file event_queue_bound_pass.hpp
 * @brief Static event queue bound estimation and burst arrival verification pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_interface.hpp"

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
class EventQueueBoundPass : public ::fsm::middleend::IPass {
  public:
    /**
     * @brief Computes event queue upper bounds and annotates the model.
     * @param ir Target FsmIr to inspect and annotate.
     * @param diag Diagnostic reporting engine.
     * @return True on success.
     */
    [[nodiscard]] std::string name() const override { return "event-queue-bound"; }
    [[nodiscard]] std::string description() const override {
        return "Computes static upper bounds on required runtime event queue capacity.";
    }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::analysis

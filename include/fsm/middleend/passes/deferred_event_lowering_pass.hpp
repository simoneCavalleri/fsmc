/**
 * @file deferred_event_lowering_pass.hpp
 * @brief Middle-End Lowering Pass: Deferred Events into Explicit Bounded Buffers and Recall Logic.
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
 * @class DeferredEventLoweringPass
 * @brief Structural lowering pass that replaces UML/SCXML deferred event declarations with
 *        explicit `__deferred_count` / `__deferred_buffer` registers and internal self-transitions.
 *
 * @note Intended for targets without native deferred-event queues; do not run before C++17/C++20
 *       code generation if the runtime's native `deferred_queue_` recall behavior is required.
 */
class DeferredEventLoweringPass : public ::fsm::middleend::IPass {
  public:
    /**
     * @brief Lowers deferred events across all state nodes in the IR.
     * @param ir Target FSM IR to mutate.
     * @param diag Diagnostic engine for errors and warnings.
     * @return True if model was modified, false otherwise.
     */
    [[nodiscard]] std::string name() const override { return "deferred-event-lowering"; }
    [[nodiscard]] std::string description() const override {
        return "Lowers deferred events into explicit software queue state machine transitions.";
    }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

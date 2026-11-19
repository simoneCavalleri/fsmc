/**
 * @file deferred_event_lowering_pass.hpp
 * @brief Middle-End Lowering Pass: Deferred Events into Explicit Bounded Buffers and Recall Logic.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

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
class DeferredEventLoweringPass {
  public:
    [[nodiscard]] static std::string name() { return "DeferredEventLowering"; }
    [[nodiscard]] static std::string description() {
        return "Lowers deferred events into explicit bounded buffers and recall transitions";
    }

    /**
     * @brief Lowers deferred events across all state nodes in the IR.
     * @param ir Target FSM IR to mutate.
     * @param diag Diagnostic engine for errors and warnings.
     * @return True if model was modified, false otherwise.
     */
    static bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

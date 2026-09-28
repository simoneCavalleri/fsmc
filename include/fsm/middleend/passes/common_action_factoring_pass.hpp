/**
 * @file common_action_factoring_pass.hpp
 * @brief Common action factoring pass for target-agnostic FSM IR.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class CommonActionFactoringPass
 * @brief Middle-End Optimization Pass: Common Action Factoring (CAF).
 *
 * Identifies convergent transitions entering the same target state (or divergent
 * transitions exiting the same source state) that share identical action sequences
 * or operations, and factors them into target state entry actions (or source state
 * exit actions). This eliminates code duplication across transition handlers,
 * minimizing synthesized ROM/code size on resource-constrained embedded targets.
 */
class CommonActionFactoringPass {
  public:
    [[nodiscard]] static std::string name() { return "CommonActionFactoring"; }
    [[nodiscard]] static std::string description() {
        return "Factors identical action sequences across convergent/divergent transitions into entry/exit actions";
    }

    /**
     * @brief Executes common action factoring on the IR model.
     * @param ir FSM IR to optimize.
     * @param diag Diagnostic engine.
     * @return True on success.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

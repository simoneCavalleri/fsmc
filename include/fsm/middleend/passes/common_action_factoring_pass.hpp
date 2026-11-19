/**
 * @file common_action_factoring_pass.hpp
 * @brief Common action factoring pass for target-agnostic FSM IR.
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
 * @class CommonActionFactoringPass
 * @brief Middle-End Optimization Pass: Common Action Factoring (CAF).
 *
 * Identifies convergent transitions entering the same target state (or divergent
 * transitions exiting the same source state) that share identical action sequences
 * or operations, and factors them into target state entry actions (or source state
 * exit actions). This eliminates code duplication across transition handlers,
 * minimizing synthesized ROM/code size on resource-constrained embedded targets.
 */
class CommonActionFactoringPass : public ::fsm::middleend::IPass {
  public:
/**
     * @brief Executes common action factoring on the IR model.
     * @param ir FSM IR to optimize.
     * @param diag Diagnostic engine.
     * @return True on success.
     */
    [[nodiscard]] std::string name() const override { return "common-action-factoring"; }
    [[nodiscard]] std::string description() const override { return "Hoists common entry/exit actions across sibling transitions into state hooks."; }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

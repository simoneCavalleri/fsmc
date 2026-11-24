/**
 * @file dead_action_elimination_pass.hpp
 * @brief Dead action elimination pass for target-agnostic FSM IR.
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
 * @class DeadActionEliminationPass
 * @brief Middle-End Optimization Pass: Dead Store and Redundant Action Elimination.
 *
 * Traverses action ASTs in state entry/exit actions and transition edges. Detects:
 * 1. Overwritten stores without intervening reads (dead write-after-write).
 * 2. Stores to variables that are unread across the entire model.
 * 3. Identity assignments (x = x).
 * Eliminates redundant operations, reducing target memory footprint and synthesis area.
 */
class DeadActionEliminationPass : public ::fsm::middleend::IPass {
  public:
    /**
     * @brief Executes dead action elimination on the IR model.
     * @param ir FSM IR to optimize.
     * @param diag Diagnostic engine.
     * @return True on success.
     */
    [[nodiscard]] std::string name() const override { return "dead-action-elimination"; }
    [[nodiscard]] std::string description() const override {
        return "Eliminates dead register writes whose values are never read on any reachable path.";
    }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

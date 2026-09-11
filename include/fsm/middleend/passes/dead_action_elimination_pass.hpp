/**
 * @file dead_action_elimination_pass.hpp
 * @brief Dead action elimination pass for target-agnostic FSM IR.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

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
class DeadActionEliminationPass {
  public:
    [[nodiscard]] static std::string name() { return "DeadActionElimination"; }
    [[nodiscard]] static std::string description() {
        return "Eliminates dead variable writes, identity assignments, and unread action operations";
    }

    /**
     * @brief Executes dead action elimination on the IR model.
     * @param ir FSM IR to optimize.
     * @param diag Diagnostic engine.
     * @return True on success.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

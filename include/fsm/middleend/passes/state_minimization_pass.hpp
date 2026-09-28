/**
 * @file state_minimization_pass.hpp
 * @brief DFA state minimization pass via Hopcroft/Moore equivalence partitioning.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class StateMinimizationPass
 * @brief Middle-End Optimization Pass: DFA State Minimization (Hopcroft/Moore Partitioning).
 *
 * Merges bisimilar/equivalent states that exhibit identical input/output transition
 * behaviors, entry/exit actions, and hierarchical parent contexts, significantly reducing
 * ROM lookup table size for embedded targets.
 */
class StateMinimizationPass {
  public:
    [[nodiscard]] static std::string name() { return "StateMinimization"; }
    [[nodiscard]] static std::string description() {
        return "Minimizes state count by merging behaviorally equivalent states";
    }

    /**
     * @brief Computes bisimulation equivalence classes and collapses redundant states.
     * @param ir Target FsmIr to minimize.
     * @param diag Diagnostic reporting engine.
     * @return True on success, false on failure.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

/**
 * @file state_minimization_pass.hpp
 * @brief DFA state minimization pass via Hopcroft/Moore equivalence partitioning.
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
 * @class StateMinimizationPass
 * @brief Middle-End Optimization Pass: DFA State Minimization (Hopcroft/Moore Partitioning).
 *
 * Merges bisimilar/equivalent states that exhibit identical input/output transition
 * behaviors, entry/exit actions, and hierarchical parent contexts, significantly reducing
 * ROM lookup table size for embedded targets.
 */
class StateMinimizationPass : public ::fsm::middleend::IPass {
  public:
/**
     * @brief Computes bisimulation equivalence classes and collapses redundant states.
     * @param ir Target FsmIr to minimize.
     * @param diag Diagnostic reporting engine.
     * @return True on success, false on failure.
     */
    [[nodiscard]] std::string name() const override { return "state-minimization"; }
    [[nodiscard]] std::string description() const override { return "Minimizes equivalent states using Hopcroft/Moore DFA partition refinement."; }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

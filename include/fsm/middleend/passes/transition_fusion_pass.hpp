/**
 * @file transition_fusion_pass.hpp
 * @brief Transition fusion pass for collapsing micro-transitions and transient states.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class TransitionFusionPass
 * @brief Middle-End Optimization Pass: Transition and Intermediate State Fusion.
 *
 * Identifies transient intermediate states traversed by deterministic microsteps
 * (unconditional or immediate completions with no external event triggers) and collapses
 * them into atomic transition edges. Composes guard conjunctions, chains action AST
 * instruction sequences, and merges clock resets, eliminating spurious reaction cycles.
 */
class TransitionFusionPass {
  public:
    [[nodiscard]] static std::string name() { return "TransitionFusion"; }
    [[nodiscard]] static std::string description() {
        return "Collapses transient micro-states and fuses consecutive transitions into direct edges";
    }

    /**
     * @brief Executes transition fusion across the IR model.
     * @param ir FSM IR to optimize.
     * @param diag Diagnostic reporting engine.
     * @return True on success.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

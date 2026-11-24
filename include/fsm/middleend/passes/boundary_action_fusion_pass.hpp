/**
 * @file boundary_action_fusion_pass.hpp
 * @brief Middle-End Lowering Pass: LCA Boundary Exit/Entry Action Cascades into Atomic Transition Action Sequences.
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
 * @class BoundaryActionFusionPass
 * @brief Computes the Least Common Ancestor (LCA) for hierarchical state transitions and fuses
 *        exit action cascades, transition actions, and entry action cascades into a single atomic
 *        action sequence directly attached to the transition edge.
 */
class BoundaryActionFusionPass : public ::fsm::middleend::IPass {
  public:
    /**
     * @brief Fuses boundary action cascades across all transition edges in the IR.
     * @param ir FSM IR model to mutate.
     * @param diag Diagnostic engine for errors and warnings.
     * @return True if actions were fused, false if no transitions required fusion.
     */
    [[nodiscard]] std::string name() const override { return "boundary-action-fusion"; }
    [[nodiscard]] std::string description() const override {
        return "Fuses LCA hierarchical exit/entry boundary actions into atomic transition actions.";
    }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

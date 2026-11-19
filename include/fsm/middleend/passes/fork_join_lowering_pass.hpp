/**
 * @file fork_join_lowering_pass.hpp
 * @brief Middle-End Lowering Pass: Parallel Fork and Join Pseudostates into Concurrent Region Activations and
 * Rendezvous Barriers.
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
 * @class ForkJoinLoweringPass
 * @brief Lowers UML/SysML Fork (split) and Join (rendezvous) pseudostates prior to Cartesian product
 *        expansion or flat sequential synthesis.
 */
class ForkJoinLoweringPass : public ::fsm::middleend::IPass {
  public:
/**
     * @brief Lowers fork and join pseudostates across the model.
     * @param ir FSM IR to mutate.
     * @param diag Diagnostic engine.
     * @return True if model was modified, false otherwise.
     */
    [[nodiscard]] std::string name() const override { return "fork-join-lowering"; }
    [[nodiscard]] std::string description() const override { return "Lowers UML Fork and Join pseudostates into synchronized parallel region transitions."; }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

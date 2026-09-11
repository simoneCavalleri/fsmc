/**
 * @file fork_join_lowering_pass.hpp
 * @brief Middle-End Lowering Pass: Parallel Fork and Join Pseudostates into Concurrent Region Activations and
 * Rendezvous Barriers.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class ForkJoinLoweringPass
 * @brief Lowers UML/SysML Fork (split) and Join (rendezvous) pseudostates prior to Cartesian product
 *        expansion or flat sequential synthesis.
 */
class ForkJoinLoweringPass {
  public:
    [[nodiscard]] static std::string name() { return "ForkJoinLowering"; }
    [[nodiscard]] static std::string description() {
        return "Lowers Fork and Join pseudostates into parallel region activations and synchronized rendezvous "
               "barriers";
    }

    /**
     * @brief Lowers fork and join pseudostates across the model.
     * @param ir FSM IR to mutate.
     * @param diag Diagnostic engine.
     * @return True if model was modified, false otherwise.
     */
    static bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

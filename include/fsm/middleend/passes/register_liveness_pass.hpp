/**
 * @file register_liveness_pass.hpp
 * @brief Register liveness analysis and storage minimization pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class RegisterLivenessPass
 * @brief Middle-End Optimization Pass: Live-range analysis and register allocation.
 *
 * Computes def-use chains for Extended Finite State Machine (EFSM) variables across states
 * and transitions. Constructs an interference graph where edges connect variables whose
 * live intervals overlap. Performs graph coloring to assign minimal register indices,
 * enabling physical register reuse in hardware synthesis (VHDL/Verilog) and minimizing
 * memory footprint in embedded targets (C/Rust/Ada).
 */
class RegisterLivenessPass {
  public:
    [[nodiscard]] static std::string name() { return "RegisterLiveness"; }
    [[nodiscard]] static std::string description() {
        return "Performs variable live-range analysis and minimizes hardware registers via graph coloring";
    }

    /**
     * @brief Executes liveness analysis and assigns register indices to variables.
     * @param ir Target FsmIr.
     * @param diag Diagnostic engine.
     * @return True on success.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

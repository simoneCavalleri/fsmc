/**
 * @file register_liveness_pass.hpp
 * @brief Register liveness analysis and storage minimization pass.
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
 * @class RegisterLivenessPass
 * @brief Middle-End Optimization Pass: Live-range analysis and register allocation.
 *
 * Computes def-use chains for Extended Finite State Machine (EFSM) variables across states
 * and transitions. Constructs an interference graph where edges connect variables whose
 * live intervals overlap. Performs graph coloring to assign minimal register indices,
 * enabling physical register reuse in hardware synthesis (VHDL/Verilog) and minimizing
 * memory footprint in embedded targets (C/Rust/Ada).
 */
class RegisterLivenessPass : public ::fsm::middleend::IPass {
  public:
/**
     * @brief Executes liveness analysis and assigns register indices to variables.
     * @param ir Target FsmIr.
     * @param diag Diagnostic engine.
     * @return True on success.
     */
    [[nodiscard]] std::string name() const override { return "register-liveness"; }
    [[nodiscard]] std::string description() const override { return "Performs backward dataflow liveness analysis and interference-graph register coloring."; }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

/**
 * @file timed_invariants_verifier_pass.hpp
 * @brief Timed Automata permanence invariants and timelock verification pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::analysis {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class TimedInvariantsVerifierPass
 * @brief Middle-End Formal Verification Pass: Verifies Timed Automata Invariants vs Transition Guards.
 *
 * Enforces formal Timed Automata permanence semantics:
 * 1. Analyzes state permanence constraints (e.g. stay_duration <= C).
 * 2. Compares against outgoing transition delays and clock guards (e.g. after(D) or clock >= D).
 * 3. Detects temporal deadlocks (timelocks) where C < D, rendering the state inescapable.
 * Reports safety-critical errors (E0403) to prevent unrecoverable temporal lockups in RTOS and FPGA logic.
 */
class TimedInvariantsVerifierPass {
  public:
    [[nodiscard]] static std::string name() { return "TimedInvariantsVerifier"; }
    [[nodiscard]] static std::string description() {
        return "Verifies Timed Automata state permanence invariants and detects temporal deadlocks (timelocks)";
    }

    /**
     * @brief Executes invariant and timelock verification across all states.
     * @param ir Target FsmIr to verify.
     * @param diag Diagnostic reporting engine.
     * @return True if all timed invariants are consistent without timelocks.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::analysis

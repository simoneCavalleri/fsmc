/**
 * @file history_lowering_pass.hpp
 * @brief Middle-End Lowering Pass: Shallow and Deep History Pseudostates into Shadow Registers and Restore Branching.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class HistoryLoweringPass
 * @brief Lowers UML/SCXML ShallowHistory and DeepHistory pseudostates into target-agnostic
 *        shadow state variables, substate exit recorders, and conditional restore branches.
 */
class HistoryLoweringPass {
  public:
    [[nodiscard]] static std::string name() { return "HistoryLowering"; }
    [[nodiscard]] static std::string description() {
        return "Lowers ShallowHistory and DeepHistory pseudostates into shadow state variables and restore transitions";
    }

    /**
     * @brief Lowers history pseudostates in the model.
     * @param ir FSM IR to transform.
     * @param diag Diagnostic engine for warnings and errors.
     * @return True on successful transformation.
     */
    static bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

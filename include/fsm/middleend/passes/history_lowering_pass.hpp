/**
 * @file history_lowering_pass.hpp
 * @brief Middle-End Lowering Pass: Shallow and Deep History Pseudostates into Shadow Registers and Restore Branching.
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
 * @class HistoryLoweringPass
 * @brief Lowers UML/SCXML ShallowHistory and DeepHistory pseudostates into target-agnostic
 *        shadow state variables, substate exit recorders, and conditional restore branches.
 */
class HistoryLoweringPass : public ::fsm::middleend::IPass {
  public:
    /**
     * @brief Lowers history pseudostates in the model.
     * @param ir FSM IR to transform.
     * @param diag Diagnostic engine for warnings and errors.
     * @return True on successful transformation.
     */
    [[nodiscard]] std::string name() const override { return "history-lowering"; }
    [[nodiscard]] std::string description() const override {
        return "Lowers UML shallow and deep history pseudostates into explicit register state tracking.";
    }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

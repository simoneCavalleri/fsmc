/**
 * @file constant_folding_pass.hpp
 * @brief Constant folding and dead branch elimination pass.
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
 * @class ConstantFoldingPass
 * @brief Middle-End Optimization Pass: Constant Folding and Dead Transition Elimination.
 */
class ConstantFoldingPass : public ::fsm::middleend::IPass {
  public:
/**
     * @brief Evaluates static expressions and prunes dead transitions across the model.
     * @param ir Target FsmIr to optimize.
     * @param diag Diagnostic reporting engine.
     * @return True on success, false on fatal transform failure.
     */
    [[nodiscard]] std::string name() const override { return "constant-folding"; }
    [[nodiscard]] std::string description() const override { return "Folds constant expressions and propagates compile-time known values."; }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

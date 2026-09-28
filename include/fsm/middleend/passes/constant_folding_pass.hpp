/**
 * @file constant_folding_pass.hpp
 * @brief Constant folding and dead branch elimination pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class ConstantFoldingPass
 * @brief Middle-End Optimization Pass: Constant Folding and Dead Transition Elimination.
 */
class ConstantFoldingPass {
  public:
    [[nodiscard]] static std::string name() { return "ConstantFolding"; }
    [[nodiscard]] static std::string description() {
        return "Folds constant guard expressions, eliminates dead transitions, and cleans unused actions";
    }

    /**
     * @brief Evaluates static expressions and prunes dead transitions across the model.
     * @param ir Target FsmIr to optimize.
     * @param diag Diagnostic reporting engine.
     * @return True on success, false on fatal transform failure.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

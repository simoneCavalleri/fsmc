/**
 * @file choice_inlining_pass.hpp
 * @brief Choice and junction pseudostate inlining pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class ChoiceInliningPass
 * @brief Target-Agnostic Middle-End Pass: Choice & Junction Pseudostate Inlining.
 */
class ChoiceInliningPass {
  public:
    [[nodiscard]] static std::string name() { return "ChoiceInlining"; }
    [[nodiscard]] static std::string description() {
        return "Inlines choice and junction pseudostates into direct composite transitions on the IR graph";
    }

    /**
     * @brief Inlines choice pseudostates into direct transitions across the IR model.
     * @param ir Target FsmIr to mutate.
     * @param diag Diagnostic reporting engine.
     * @return True on success, false on fatal transform failure.
     */
    static bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

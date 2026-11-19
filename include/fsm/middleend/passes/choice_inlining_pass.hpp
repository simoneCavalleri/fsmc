/**
 * @file choice_inlining_pass.hpp
 * @brief Choice and junction pseudostate inlining pass.
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
 * @class ChoiceInliningPass
 * @brief Target-Agnostic Middle-End Pass: Choice & Junction Pseudostate Inlining.
 */
class ChoiceInliningPass : public ::fsm::middleend::IPass {
  public:
/**
     * @brief Inlines choice pseudostates into direct transitions across the IR model.
     * @param ir Target FsmIr to mutate.
     * @param diag Diagnostic reporting engine.
     * @return True on success, false on fatal transform failure.
     */
    [[nodiscard]] std::string name() const override { return "choice-inlining"; }
    [[nodiscard]] std::string description() const override { return "Inlines UML Choice pseudostates into guarded direct transitions."; }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

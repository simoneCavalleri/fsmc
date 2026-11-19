/**
 * @file guard_simplification_pass.hpp
 * @brief Algebraic simplification and canonicalization of guard ASTs pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/middleend/pass_interface.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;
using ir::GuardAstNode;

/**
 * @class GuardSimplificationPass
 * @brief Target-Agnostic Middle-End Pass: Algebraic simplification and canonicalization of Guard ASTs.
 */
class GuardSimplificationPass : public ::fsm::middleend::IPass {
  public:
    /**
     * @brief Simplifies a single GuardAstNode recursively.
     */
    static GuardAstNode simplify_node(const GuardAstNode& node);

    /**
     * @brief Traverses and simplifies guard expressions on all transitions in the model.
     * @param ir Target FsmIr to optimize.
     * @param diag Diagnostic engine for errors.
     * @return True on success, false on failure.
     */
    [[nodiscard]] std::string name() const override { return "guard-simplification"; }
    [[nodiscard]] std::string description() const override {
        return "Simplifies redundant boolean guard expressions and algebraic tautologies.";
    }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

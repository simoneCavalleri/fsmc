/**
 * @file guard_simplification_pass.hpp
 * @brief Algebraic simplification and canonicalization of guard ASTs pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/guard.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;
using ir::GuardAstNode;

/**
 * @class GuardSimplificationPass
 * @brief Target-Agnostic Middle-End Pass: Algebraic simplification and canonicalization of Guard ASTs.
 */
class GuardSimplificationPass {
  public:
    [[nodiscard]] static std::string name() { return "GuardSimplification"; }
    [[nodiscard]] static std::string description() {
        return "Performs algebraic simplification, constant folding, and canonicalization of guard ASTs";
    }

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
    bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

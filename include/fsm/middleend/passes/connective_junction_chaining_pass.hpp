/**
 * @file connective_junction_chaining_pass.hpp
 * @brief Static connective junction chaining and compound transition synthesis pass.
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
 * @class ConnectiveJunctionChainingPass
 * @brief Target-Agnostic Middle-End Pass: Connective Junction Chaining.
 *
 * Resolves multi-hop connective junction flow diagrams (Stateflow / SysML / UML)
 * into direct compound transitions with conjoined guards and sequenced actions.
 */
class ConnectiveJunctionChainingPass : public ::fsm::middleend::IPass {
  public:
    /**
     * @brief Traverses acyclic connective junction paths and collapses them into direct transitions.
     * @param ir Target FsmIr to mutate.
     * @param diag Diagnostic reporting engine.
     * @return True if any junctions were chained and eliminated, false otherwise.
     */
    [[nodiscard]] std::string name() const override { return "connective-junction-chaining"; }
    [[nodiscard]] std::string description() const override {
        return "Chains multi-hop connective junction paths into atomic composite transitions.";
    }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

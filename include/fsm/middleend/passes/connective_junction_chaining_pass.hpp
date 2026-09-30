/**
 * @file connective_junction_chaining_pass.hpp
 * @brief Static connective junction chaining and compound transition synthesis pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

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
class ConnectiveJunctionChainingPass {
  public:
    [[nodiscard]] static std::string name() { return "ConnectiveJunctionChaining"; }
    [[nodiscard]] static std::string description() {
        return "Chains multi-hop connective junction paths into direct atomic compound transitions";
    }

    /**
     * @brief Traverses acyclic connective junction paths and collapses them into direct transitions.
     * @param ir Target FsmIr to mutate.
     * @param diag Diagnostic reporting engine.
     * @return True if any junctions were chained and eliminated, false otherwise.
     */
    static bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

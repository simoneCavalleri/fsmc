/**
 * @file verify_ir_integrity_pass.hpp
 * @brief Mandatory entry integrity verification pass for canonical and externally ingested FsmIr models.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_interface.hpp"

namespace fsm::middleend::passes {

/**
 * @class VerifyIrIntegrityPass
 * @brief Validates structural and semantic invariants of an FsmIr before middle-end passes or model checking.
 *
 * Checks:
 *  - E_IR_ROOT         : Non-empty model name, valid concurrency semantics, resolvable initial state.
 *  - E_IR_DUP_ID       : Unique state IDs/FQNs, port names, variable names, and custom type names.
 *  - E_IR_HIERARCHY    : Valid parent_state / initial_sub_state links, acyclic hierarchy, valid history parents.
 *  - E_IR_DANGLING_REF : All transition source/target endpoints resolve to declared states.
 *  - E_IR_TYPE_MISMATCH: Prohibits writing to InPorts in datapath assignments/stores and validates LValue targets.
 *  - E_IR_BAD_SPAN     : Validates source span metadata consistency.
 */
class VerifyIrIntegrityPass : public ::fsm::middleend::IPass {
  public:
    [[nodiscard]] std::string name() const override { return "verify-ir-integrity"; }
    [[nodiscard]] std::string description() const override {
        return "Verifies mandatory FsmIr structural invariants (IDs, acyclic hierarchy, endpoints, types, spans).";
    }
    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }

    bool run(ir::FsmIr& ir, diagnostic::DiagnosticEngine& diag) override;
};

}  // namespace fsm::middleend::passes

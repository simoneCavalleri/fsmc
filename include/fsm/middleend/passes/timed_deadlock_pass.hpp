/**
 * @file timed_deadlock_pass.hpp
 * @brief Race condition detection between state timeouts and immediate transitions pass.
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
 * @class TimedDeadlockPass
 * @brief Middle-end verification pass detecting conflicting timed transitions and immediate transitions.
 */
class TimedDeadlockPass : public ::fsm::middleend::IPass {
  public:
/**
     * @brief Inspects timeout triggers and immediate transitions exiting the same states.
     * @param ir Read-only FsmIr model to analyze.
     * @param diag Diagnostic engine for warnings.
     * @return True if no unprioritized timeout race conflicts exist, false otherwise.
     */
    [[nodiscard]] std::string name() const override { return "timed-deadlock"; }
    [[nodiscard]] std::string description() const override { return "Detects timed deadlocks and Zeno zero-time cycles in timed automata."; }
    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }
    bool run(const FsmIr& ir, DiagnosticEngine& diag) const;
    bool run(FsmIr& ir, DiagnosticEngine& diag) override { return run(static_cast<const FsmIr&>(ir), diag); }
};

}  // namespace fsm::middleend::passes

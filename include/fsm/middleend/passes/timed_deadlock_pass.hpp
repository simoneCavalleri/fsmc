/**
 * @file timed_deadlock_pass.hpp
 * @brief Race condition detection between state timeouts and immediate transitions pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class TimedDeadlockPass
 * @brief Middle-end verification pass detecting conflicting timed transitions and immediate transitions.
 */
class TimedDeadlockPass {
  public:
    [[nodiscard]] static std::string name() { return "TimedDeadlock"; }
    [[nodiscard]] static std::string description() {
        return "Verifies that state timeouts do not race with immediate transitions without explicit priority";
    }

    /**
     * @brief Inspects timeout triggers and immediate transitions exiting the same states.
     * @param ir Read-only FsmIr model to analyze.
     * @param diag Diagnostic engine for warnings.
     * @return True if no unprioritized timeout race conflicts exist, false otherwise.
     */
    bool run(const FsmIr& ir, DiagnosticEngine& diag) const;
};

}  // namespace fsm::middleend::passes

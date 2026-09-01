#pragma once

#include <string>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/trigger.hpp"

namespace fsm::codegen {

/**
 * @brief Middle-end verification pass detecting conflicting timed transitions and immediate transitions.
 *
 * Checks:
 * 1. Conflicting TimeTrigger vs immediate transitions from the same source state lacking explicit priority.
 * 2. Zero-duration or negative timeouts.
 * 3. Multiple unprioritized TimeTriggers on the same state with identical or overlapping timeout intervals.
 */
class TimedDeadlockPass {
  public:
    [[nodiscard]] static std::string name() { return "TimedDeadlock"; }
    [[nodiscard]] static std::string description() {
        return "Verifies that state timeouts do not race with immediate transitions without explicit priority";
    }

    bool run(const FsmIr& ir, DiagnosticEngine& diag) const {
        for (const auto& st : ir.states) {
            std::vector<const TransitionEdge*> timed_transitions;
            std::vector<const TransitionEdge*> immediate_transitions;

            for (const auto& trans : ir.transitions) {
                if (trans.source == st.name || trans.source_id == st.name) {
                    if (std::holds_alternative<TimeTrigger>(trans.trigger)) {
                        timed_transitions.push_back(&trans);
                    } else {
                        immediate_transitions.push_back(&trans);
                    }
                }
            }

            if (timed_transitions.empty()) {
                continue;
            }

            // 1. Check zero duration
            for (const auto* tt : timed_transitions) {
                const auto& trigger = std::get<TimeTrigger>(tt->trigger);
                if (trigger.duration_in_ms() == 0 && trigger.dynamic_expression.empty()) {
                    diag.report(Diagnostic::warning(
                        "W0401", "State '" + st.name + "' has a TimeTrigger with 0ms duration (immediate timeout)."));
                }
            }

            // 2. Check multiple unprioritized timed transitions with same duration
            for (size_t i = 0; i < timed_transitions.size(); ++i) {
                const auto& t1 = std::get<TimeTrigger>(timed_transitions[i]->trigger);
                for (size_t j = i + 1; j < timed_transitions.size(); ++j) {
                    const auto& t2 = std::get<TimeTrigger>(timed_transitions[j]->trigger);
                    if (t1.duration_in_ms() == t2.duration_in_ms() &&
                        timed_transitions[i]->priority == timed_transitions[j]->priority &&
                        timed_transitions[i]->guard == timed_transitions[j]->guard) {
                        diag.report(Diagnostic::warning("W0402", "State '" + st.name +
                                                                     "' has multiple identical TimeTriggers with equal "
                                                                     "priority (non-deterministic timeout dispatch)."));
                    }
                }
            }

            // 3. Check conflict with immediate unprioritized transitions
            for (const auto* imm : immediate_transitions) {
                for (const auto* timed : timed_transitions) {
                    if (imm->priority == timed->priority && imm->priority == 0) {
                        std::string imm_name = imm->event.empty() ? "Anonymous" : imm->event;
                        diag.report(Diagnostic::warning("W0403", "State '" + st.name + "' has timeout transition (" +
                                                                     timed->get_trigger_name() +
                                                                     ") racing with immediate transition (" + imm_name +
                                                                     ") without explicit priority ordering."));
                    }
                }
            }
        }

        return true;
    }
};

}  // namespace fsm::codegen

namespace fsm {
using TimedDeadlockPass = ::fsm::codegen::TimedDeadlockPass;
}  // namespace fsm

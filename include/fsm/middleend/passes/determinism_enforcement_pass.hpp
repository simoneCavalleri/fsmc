#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

/**
 * @brief Target-Agnostic Middle-End Pass: Enforces transition determinism and canonical priority ordering.
 *
 * 1. Groups transitions sharing the same (source, event) trigger.
 * 2. Detects non-deterministic conflicts where multiple branches lack distinct guards or priority differentiation.
 * 3. Re-orders the IR transition table so higher-priority transitions (lower priority integer) always precede
 * lower-priority ones.
 */
class DeterminismEnforcementPass {
  public:
    [[nodiscard]] static std::string name() { return "DeterminismEnforcement"; }
    [[nodiscard]] static std::string description() {
        return "Enforces deterministic event dispatching and applies priority-based canonical ordering";
    }

    bool run(FsmIr& ir, DiagnosticEngine& diag) {
        // Group transitions by (source, event)
        std::map<std::pair<std::string, std::string>, std::vector<TransitionEdge*>> groups;
        for (auto& t : ir.transitions) {
            std::string src = t.source_id.empty() ? t.source : t.source_id;
            groups[{src, t.event}].push_back(&t);
        }

        for (const auto& [key, trans_list] : groups) {
            const auto& [src, evt] = key;
            if (trans_list.size() <= 1)
                continue;

            // Count unconditional transitions (no guard)
            std::size_t unconditional_count = 0;
            std::map<std::uint32_t, std::size_t> priority_counts;

            for (const auto* t : trans_list) {
                if (!t->guard.has_value() || t->guard->empty()) {
                    ++unconditional_count;
                }
                ++priority_counts[t->priority];
            }

            // If multiple unconditional transitions share the same priority -> non-deterministic collision!
            if (unconditional_count > 1) {
                bool has_priority_tie = false;
                for (const auto& [prio, cnt] : priority_counts) {
                    if (cnt > 1) {
                        has_priority_tie = true;
                        break;
                    }
                }
                if (has_priority_tie) {
                    diag.report(Diagnostic::safety_critical(
                        "W_NONDETERMINISTIC_CONFLICT",
                        "State '" + src + "' has multiple non-deterministic outgoing transitions for event '" + evt +
                            "' with identical priority levels."));
                }
            }
        }

        // Sort entire transition table canonically: (source, event, priority, target)
        std::stable_sort(ir.transitions.begin(), ir.transitions.end(),
                         [](const TransitionEdge& a, const TransitionEdge& b) {
                             if (a.source != b.source)
                                 return a.source < b.source;
                             if (a.event != b.event)
                                 return a.event < b.event;
                             if (a.priority != b.priority)
                                 return a.priority < b.priority;  // Lower number = higher priority
                             return a.target < b.target;
                         });

        return true;
    }
};

}  // namespace fsm::codegen

namespace fsm {
using DeterminismEnforcementPass = ::fsm::codegen::DeterminismEnforcementPass;
}  // namespace fsm

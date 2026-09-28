/**
 * @file timed_invariants_verifier_pass.cpp
 * @brief Implementation of TimedInvariantsVerifierPass.
 */

#include "fsm/middleend/analysis/timed_invariants_verifier_pass.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fsm::middleend::analysis {

namespace {

std::string resolve_id(const ir::FsmIr& ir, const std::string& id_or_name) {
    if (auto* s = ir.find_state_by_id(id_or_name))
        return s->id;
    if (auto* s = ir.find_state_by_name(id_or_name))
        return s->id;
    return id_or_name;
}

}  // namespace

bool TimedInvariantsVerifierPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    bool fatal_timelock = false;

    for (const auto& state : ir.states) {
        if (state.kind == ir::StateKind::Terminate || state.kind == ir::StateKind::Final) {
            continue;
        }

        std::uint64_t max_stay_ms = UINT64_MAX;
        bool has_permanence_bound = false;

        if (state.time_invariant.has_value() && !state.time_invariant->empty()) {
            max_stay_ms = ir::to_milliseconds(state.time_invariant->duration, state.time_invariant->unit);
            has_permanence_bound = true;
        }

        if (!has_permanence_bound) {
            continue;
        }

        // Find all outgoing transitions
        std::vector<const ir::TransitionEdge*> outgoing;
        for (const auto& edge : ir.transitions) {
            std::string src = resolve_id(ir, edge.source_id.empty() ? edge.source : edge.source_id);
            if (src == state.id) {
                outgoing.push_back(&edge);
            }
        }

        if (outgoing.empty()) {
            diag.report(diagnostic::Diagnostic::safety_critical(
                "E0403", "Temporal deadlock (timelock): state '" + state.name + "' has permanence invariant stay <= " +
                             std::to_string(max_stay_ms) + "ms but has no outgoing transitions."));
            fatal_timelock = true;
            continue;
        }

        // Check if ANY outgoing transition can be taken at or before max_stay_ms
        bool can_exit_before_timelock = false;

        for (const auto* edge : outgoing) {
            if (std::holds_alternative<ir::SignalTrigger>(edge->trigger)) {
                if (!std::get<ir::SignalTrigger>(edge->trigger).signal_name.empty()) {
                    can_exit_before_timelock = true;  // External event could arrive in time
                    break;
                }
            } else if (std::holds_alternative<ir::AnonymousTrigger>(edge->trigger)) {
                can_exit_before_timelock = true;  // Immediate transition
                break;
            } else if (std::holds_alternative<ir::TimeTrigger>(edge->trigger)) {
                const auto& tt = std::get<ir::TimeTrigger>(edge->trigger);
                if (tt.duration_in_ms() <= max_stay_ms) {
                    can_exit_before_timelock = true;
                    break;
                }
            } else if (std::holds_alternative<ir::ChangeTrigger>(edge->trigger)) {
                can_exit_before_timelock = true;
                break;
            }
        }

        if (!can_exit_before_timelock) {
            diag.report(diagnostic::Diagnostic::safety_critical(
                "E0403", "Temporal deadlock (timelock): state '" + state.name +
                             "' has permanence invariant stay <= " + std::to_string(max_stay_ms) +
                             "ms, but all outgoing transitions require delay exceeding invariant bound."));
            fatal_timelock = true;
        }
    }

    return !fatal_timelock;
}

}  // namespace fsm::middleend::analysis

/**
 * @file event_queue_bound_pass.cpp
 * @brief Implementation of EventQueueBoundPass.
 */

#include "fsm/middleend/analysis/event_queue_bound_pass.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>

namespace fsm::middleend::analysis {

namespace {

std::size_t count_emitted_signals(const ir::ActionSignature& act) {
    std::size_t count = 0;
    for (const auto& inst : act.instructions) {
        if (std::holds_alternative<ir::SignalEmitOp>(inst.op)) {
            ++count;
        }
    }
    return count;
}

}  // namespace

bool EventQueueBoundPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    std::size_t max_deferred = 0;
    for (const auto& state : ir.states) {
        if (state.deferred_events.size() > max_deferred) {
            max_deferred = state.deferred_events.size();
        }
    }

    std::size_t max_action_emits = 0;
    for (const auto& state : ir.states) {
        for (const auto& act : state.entry_actions) {
            max_action_emits = std::max(max_action_emits, count_emitted_signals(act));
        }
        for (const auto& act : state.exit_actions) {
            max_action_emits = std::max(max_action_emits, count_emitted_signals(act));
        }
    }

    for (const auto& edge : ir.transitions) {
        if (edge.transition_action) {
            max_action_emits = std::max(max_action_emits, count_emitted_signals(*edge.transition_action));
        }
        if (edge.condition_action) {
            max_action_emits = std::max(max_action_emits, count_emitted_signals(*edge.condition_action));
        }
    }

    // Baseline queue capacity: max_deferred + max_burst_emits + 2 headroom for incoming bus/port
    std::size_t bound = max_deferred + max_action_emits + 2;
    // Enforce power-of-two rounding for fast bitmask ring-buffer indexing
    std::size_t power_of_two_capacity = 2;
    while (power_of_two_capacity < bound) {
        power_of_two_capacity <<= 1;
    }

    ir.attributes["static_event_queue_capacity"] = std::to_string(power_of_two_capacity);
    ir.attributes["max_deferred_events_bound"] = std::to_string(max_deferred);
    ir.attributes["max_burst_emits_bound"] = std::to_string(max_action_emits);

    diag.report(diagnostic::Diagnostic::info(
        "EventQueueBound", "Estimated maximum event queue bound: " + std::to_string(power_of_two_capacity) +
                               " slots (max deferred: " + std::to_string(max_deferred) +
                               ", max burst emits: " + std::to_string(max_action_emits) + ")."));

    return true;
}

}  // namespace fsm::middleend::analysis

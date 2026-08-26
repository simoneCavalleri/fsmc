#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "fsm/ir/action.hpp"
#include "fsm/ir/deterministic_id.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/transition_edge_kind.hpp"
#include "fsm/ir/trigger.hpp"

namespace fsm::codegen {

// ============================================================================
// TransitionEdge in the Formal IR
// ============================================================================

struct TransitionEdge {
    std::string id;
    std::string source;
    std::string target;
    std::string source_id;
    std::string target_id;
    std::vector<std::string> source_ids;  ///< Multi-source endpoints for Join synchronization
    std::vector<std::string> target_ids;  ///< Multi-target endpoints for Fork synchronization
    std::string event;
    std::optional<std::string> guard;
    std::optional<std::string> action;
    TriggerVariant trigger{AnonymousTrigger{}};
    std::optional<GuardAstNode> guard_ast;
    std::optional<ActionSignature> action_sig;
    TransitionEdgeKind kind{TransitionEdgeKind::External};
    std::uint32_t priority{0};  ///< Explicit precedence (lower number = higher priority)
    std::string description;
    bool target_is_history{false};
    bool target_is_deep_history{false};
    std::string parent_scope;
    std::vector<std::string> traceability_reqs;  ///< Traceability requirement tags (e.g. "REQ-TRANS-01")

    TransitionEdge() = default;
    TransitionEdge(std::string src, std::string dst, std::string evt, std::optional<std::string> grd = std::nullopt,
                   std::optional<std::string> act = std::nullopt, std::string desc = "",
                   TransitionEdgeKind transition_kind = TransitionEdgeKind::External, std::uint32_t trans_priority = 0)
        : source(std::move(src)),
          target(std::move(dst)),
          source_id(source),
          target_id(target),
          event(std::move(evt)),
          guard(std::move(grd)),
          action(std::move(act)),
          kind(transition_kind),
          priority(trans_priority),
          description(std::move(desc)) {
        id = compute_deterministic_id(source + "->" + target + ":" + event + "[" + (guard ? *guard : "") + "]");
        if (!source_id.empty()) {
            source_ids.push_back(source_id);
        }
        if (!target_id.empty()) {
            target_ids.push_back(target_id);
        }
        if (!event.empty()) {
            trigger = SignalTrigger{event, "payload"};
        }
        if (guard.has_value()) {
            guard_ast = GuardAstNode(*guard);
        }
        if (action.has_value()) {
            action_sig = ActionSignature(*action, *action);
        }
    }
    TransitionEdge(std::string edge_id, std::string src_id, std::string dst_id,
                   TriggerVariant edge_trigger = AnonymousTrigger{}, std::uint32_t trans_priority = 0)
        : id(std::move(edge_id)),
          source(src_id),
          target(dst_id),
          source_id(std::move(src_id)),
          target_id(std::move(dst_id)),
          trigger(std::move(edge_trigger)),
          priority(trans_priority) {
        if (!source_id.empty()) {
            source_ids.push_back(source_id);
        }
        if (!target_id.empty()) {
            target_ids.push_back(target_id);
        }
        event = get_trigger_name();
    }

    [[nodiscard]] bool is_fork() const noexcept { return target_ids.size() > 1; }
    [[nodiscard]] bool is_join() const noexcept { return source_ids.size() > 1; }

    [[nodiscard]] std::string get_trigger_name() const {
        if (!event.empty())
            return event;
        if (std::holds_alternative<SignalTrigger>(trigger)) {
            return std::get<SignalTrigger>(trigger).signal_name;
        }
        if (std::holds_alternative<TimeTrigger>(trigger)) {
            const auto& t = std::get<TimeTrigger>(trigger);
            return (t.periodic ? "every_" : "after_") + std::to_string(t.duration_ms) + "ms";
        }
        return "";
    }

    bool operator==(const TransitionEdge& other) const noexcept {
        return id == other.id && source_id == other.source_id && target_id == other.target_id &&
               source_ids == other.source_ids && target_ids == other.target_ids && trigger == other.trigger &&
               guard_ast == other.guard_ast && action_sig == other.action_sig && kind == other.kind &&
               priority == other.priority && description == other.description;
    }
};

}  // namespace fsm::codegen

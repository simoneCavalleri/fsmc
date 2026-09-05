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

namespace fsm::ir {

// ============================================================================
// TransitionEdge in the Formal IR
// ============================================================================

struct TransitionEdge {
    std::string id;
    std::string source;
    std::string target;
    std::vector<std::string> source_ids;  ///< Multi-source endpoints for Join synchronization
    std::vector<std::string> target_ids;  ///< Multi-target endpoints for Fork synchronization
    std::string event;
    std::optional<std::string> guard;
    std::optional<std::string> action;
    TriggerVariant trigger{AnonymousTrigger{}};
    std::optional<GuardAstNode> guard_ast;
    std::optional<ActionSignature> action_sig;
    TransitionEdgeKind kind{TransitionEdgeKind::External};
    std::uint32_t priority{0};  ///< Execution precedence: 1 = Highest, 2 = Next, ..., 0 = Default/Unspecified
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
          event(std::move(evt)),
          guard(std::move(grd)),
          action(std::move(act)),
          kind(transition_kind),
          priority(trans_priority),
          description(std::move(desc)) {
        id = compute_deterministic_id(source + "->" + target + ":" + event + "[" + (guard ? *guard : "") + "]");
        if (!source.empty()) {
            source_ids.push_back(source);
        }
        if (!target.empty()) {
            target_ids.push_back(target);
        }
        if (!event.empty()) {
            trigger = SignalTrigger{event, "payload"};
        }
        if (guard.has_value() && !guard->empty()) {
            guard_ast = GuardAstNode(*guard);
        }
        if (action.has_value() && !action->empty()) {
            action_sig = ActionSignature(*action, *action);
        }
    }
    TransitionEdge(std::string edge_id, std::string src, std::string dst,
                   TriggerVariant edge_trigger, std::uint32_t trans_priority = 0)
        : id(std::move(edge_id)),
          source(std::move(src)),
          target(std::move(dst)),
          trigger(std::move(edge_trigger)),
          priority(trans_priority) {
        if (!source.empty()) {
            source_ids.push_back(source);
        }
        if (!target.empty()) {
            target_ids.push_back(target);
        }
        event = get_trigger_name();
    }

    [[nodiscard]] bool is_fork() const noexcept { return target_ids.size() > 1; }
    [[nodiscard]] bool is_join() const noexcept { return source_ids.size() > 1; }

    [[nodiscard]] std::string get_guard() const {
        if (guard_ast.has_value()) {
            return guard_ast->to_string();
        }
        return guard.value_or("");
    }

    [[nodiscard]] std::string get_action() const {
        if (action_sig.has_value()) {
            return action_sig->name;
        }
        return action.value_or("");
    }

    void set_guard(std::optional<std::string> g) {
        guard = std::move(g);
        if (guard.has_value() && !guard->empty()) {
            guard_ast = GuardAstNode(*guard);
        } else {
            guard_ast = std::nullopt;
            guard = std::nullopt;
        }
    }

    void set_guard_ast(std::optional<GuardAstNode> ast) {
        guard_ast = std::move(ast);
        if (guard_ast.has_value()) {
            guard = guard_ast->to_string();
        } else {
            guard = std::nullopt;
        }
    }

    void set_action(std::optional<std::string> a) {
        action = std::move(a);
        if (action.has_value() && !action->empty()) {
            action_sig = ActionSignature(*action, *action);
        } else {
            action_sig = std::nullopt;
            action = std::nullopt;
        }
    }

    void set_action_sig(std::optional<ActionSignature> sig) {
        action_sig = std::move(sig);
        if (action_sig.has_value()) {
            action = action_sig->name;
        } else {
            action = std::nullopt;
        }
    }

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

    [[nodiscard]] bool has_priority() const noexcept { return priority > 0; }

    bool operator==(const TransitionEdge& other) const noexcept {
        return id == other.id && source == other.source && target == other.target &&
               source_ids == other.source_ids && target_ids == other.target_ids && trigger == other.trigger &&
               guard_ast == other.guard_ast && action_sig == other.action_sig && kind == other.kind &&
               priority == other.priority && description == other.description;
    }
};

}  // namespace fsm::ir

namespace fsm {
using TransitionEdge = ir::TransitionEdge;
}


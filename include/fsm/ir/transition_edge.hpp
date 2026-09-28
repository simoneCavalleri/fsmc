/**
 * @file transition_edge.hpp
 * @brief Directed Transition Edge Metamodel with triggers, guards, priorities, and actions in FSM IR.
 */

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

/**
 * @brief Directed transition edge between states in the formal state graph.
 *
 * Encapsulates full transition semantics:
 * - Source and target states (including multi-source Joins and multi-target Forks)
 * - Trigger specification (Signals, Timers, Completion)
 * - Boolean guard predicates and AST
 * - Two-phase actions: Condition actions (Stateflow/SysML) and Transition effect actions
 * - Edge topology kind (External, Internal, Local)
 * - Execution priority order (1 = Highest precedence, 2 = Next, 0 = Default/Unspecified)
 */
struct TransitionEdge {
    std::string id;                              ///< Deterministic FNV-1a unique edge hash
    std::string source;                          ///< Source state name
    std::string target;                          ///< Target state name
    std::string source_id;                       ///< Deterministic ID of source state
    std::string target_id;                       ///< Deterministic ID of target state
    std::vector<std::string> source_ids;         ///< Multi-source endpoints for Join synchronization
    std::vector<std::string> target_ids;         ///< Multi-target endpoints for Fork synchronization
    std::vector<std::string> multi_source_ids;   ///< Multi-source deterministic IDs for Join synchronization
    std::vector<std::string> multi_target_ids;   ///< Multi-target deterministic IDs for Fork synchronization
    std::vector<std::string> clock_resets;       ///< Timed Automata clocks to reset upon firing (x := 0)
    std::string event;                           ///< Event or signal name string
    std::optional<std::string> guard;            ///< Raw guard predicate string
    TriggerVariant trigger{AnonymousTrigger{}};  ///< Structured trigger variant
    std::optional<GuardAstNode> guard_ast;       ///< Structured composable guard AST
    std::optional<ActionSignature> condition_action{std::nullopt};  ///< Immediate condition action [guard] / {cond_act}
    std::optional<ActionSignature> transition_action{
        std::nullopt};                                      ///< State exit/entry transition effect action / {trans_act}
    TransitionEdgeKind kind{TransitionEdgeKind::External};  ///< Transition topology semantics
    std::uint32_t priority{0};                              ///< Precedence: 1 = Highest, 2 = Next, 0 = Default
    std::string description;                                ///< Human-readable documentation comment
    bool target_is_history{false};                          ///< True if pointing to a shallow history state [H]
    bool target_is_deep_history{false};                     ///< True if pointing to a deep history state [H*]
    std::string parent_scope;                               ///< Enclosing composite state scope
    std::vector<std::string> traceability_reqs;             ///< Traceability requirement tags (e.g. "REQ-TRANS-01")

    TransitionEdge() = default;

    TransitionEdge(std::string src, std::string dst, std::string evt, std::optional<std::string> grd = std::nullopt,
                   std::optional<ActionSignature> trans_act = std::nullopt, std::string desc = "",
                   TransitionEdgeKind transition_kind = TransitionEdgeKind::External, std::uint32_t trans_priority = 0,
                   std::optional<ActionSignature> cond_act = std::nullopt)
        : source(std::move(src)),
          target(std::move(dst)),
          event(std::move(evt)),
          guard(std::move(grd)),
          condition_action(std::move(cond_act)),
          transition_action(std::move(trans_act)),
          kind(transition_kind),
          priority(trans_priority),
          description(std::move(desc)) {
        id = compute_deterministic_id(source + "->" + target + ":" + event + "[" + (guard ? *guard : "") + "]");
        if (!source.empty()) {
            source_id = compute_deterministic_id(source);
            source_ids.push_back(source);
            multi_source_ids.push_back(source_id);
        }
        if (!target.empty()) {
            target_id = compute_deterministic_id(target);
            target_ids.push_back(target);
            multi_target_ids.push_back(target_id);
        }
        if (!event.empty()) {
            trigger = SignalTrigger{event, "payload"};
        }
        if (guard.has_value() && !guard->empty()) {
            guard_ast = GuardAstNode(*guard);
        }
    }

    TransitionEdge(std::string edge_id, std::string src, std::string dst, TriggerVariant edge_trigger,
                   std::uint32_t trans_priority = 0)
        : id(std::move(edge_id)),
          source(std::move(src)),
          target(std::move(dst)),
          trigger(std::move(edge_trigger)),
          priority(trans_priority) {
        if (!source.empty()) {
            source_id = compute_deterministic_id(source);
            source_ids.push_back(source);
            multi_source_ids.push_back(source_id);
        }
        if (!target.empty()) {
            target_id = compute_deterministic_id(target);
            target_ids.push_back(target);
            multi_target_ids.push_back(target_id);
        }
        event = get_trigger_name();
    }

    /**
     * @brief Checks whether the transition splits into multiple parallel target states (Fork).
     */
    [[nodiscard]] bool is_fork() const noexcept { return target_ids.size() > 1; }

    /**
     * @brief Checks whether the transition synchronizes multiple parallel source states (Join).
     */
    [[nodiscard]] bool is_join() const noexcept { return source_ids.size() > 1; }

    /**
     * @brief Retrieves the guard expression string (from AST if available, otherwise raw).
     */
    [[nodiscard]] std::string get_guard() const {
        if (guard_ast.has_value()) {
            return guard_ast->to_string();
        }
        return guard.value_or("");
    }

    /**
     * @brief Retrieves the primary action identifier associated with this transition.
     */
    [[nodiscard]] std::string get_action() const {
        if (transition_action.has_value()) {
            return transition_action->name;
        }
        if (condition_action.has_value()) {
            return condition_action->name;
        }
        return "";
    }

    /**
     * @brief Sets or clears the raw guard string and updates the guard AST accordingly.
     */
    void set_guard(std::optional<std::string> g) {
        guard = std::move(g);
        if (guard.has_value() && !guard->empty()) {
            guard_ast = GuardAstNode(*guard);
        } else {
            guard_ast = std::nullopt;
            guard = std::nullopt;
        }
    }

    /**
     * @brief Sets or clears the guard AST and synchronizes the raw guard text.
     */
    void set_guard_ast(std::optional<GuardAstNode> ast) {
        guard_ast = std::move(ast);
        if (guard_ast.has_value()) {
            guard = guard_ast->to_string();
        } else {
            guard = std::nullopt;
        }
    }

    void set_transition_action(std::optional<ActionSignature> act) { transition_action = std::move(act); }

    void set_condition_action(std::optional<ActionSignature> act) { condition_action = std::move(act); }

    void set_action(std::optional<ActionSignature> act) { transition_action = std::move(act); }

    void set_action(const char* act) {
        if (act != nullptr && *act != '\0') {
            transition_action = ActionSignature(act);
        } else {
            transition_action = std::nullopt;
        }
    }

    /**
     * @brief Derives a canonical display name for the trigger.
     */
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
        if (std::holds_alternative<ChangeTrigger>(trigger)) {
            const auto& c = std::get<ChangeTrigger>(trigger);
            return "when(" + c.raw_expression + ")";
        }
        return "";
    }

    /**
     * @brief Checks whether this transition has an explicit non-zero priority assigned.
     */
    [[nodiscard]] bool has_priority() const noexcept { return priority > 0; }

    bool operator==(const TransitionEdge& other) const noexcept {
        return id == other.id && source == other.source && target == other.target && source_ids == other.source_ids &&
               target_ids == other.target_ids && trigger == other.trigger && guard_ast == other.guard_ast &&
               condition_action == other.condition_action && transition_action == other.transition_action &&
               kind == other.kind && priority == other.priority && description == other.description;
    }
};

}  // namespace fsm::ir

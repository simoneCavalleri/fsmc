/**
 * @file state_node.hpp
 * @brief Hierarchical State Node Metamodel and Timed Automata Permanence Invariants in FSM IR.
 */

#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/ir/action.hpp"
#include "fsm/ir/deterministic_id.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/region.hpp"
#include "fsm/ir/state_kind.hpp"
#include "fsm/ir/trigger.hpp"

namespace fsm::ir {

/**
 * @brief Relational comparison operator for state time invariants.
 */
enum class TimeInvariantOp : std::uint8_t {
    LessEqual,  ///< Less than or equal to (<=)
    LessThan    ///< Strictly less than (<)
};

/**
 * @brief Converts a TimeInvariantOp enum to its string representation ("<=", "<").
 */
[[nodiscard]] constexpr std::string_view time_invariant_op_to_string(TimeInvariantOp op) noexcept {
    switch (op) {
        case TimeInvariantOp::LessEqual:
            return "<=";
        case TimeInvariantOp::LessThan:
            return "<";
    }
    return "<=";
}

/**
 * @brief Structured Timed Automata permanence constraint (e.g. "stay_duration <= 500ms").
 */
struct StateTimeInvariant {
    std::string clock{"stay_duration"};              ///< Clock variable identifier
    TimeInvariantOp op{TimeInvariantOp::LessEqual};  ///< Relational operator (<=, <)
    std::uint64_t duration{0};                       ///< Numeric duration value
    TimeUnit unit{TimeUnit::Milliseconds};           ///< Duration physical unit
    std::string raw_expression;                      ///< Original or formatted invariant expression

    StateTimeInvariant() = default;
    /* implicit */ StateTimeInvariant(std::string_view str);
    /* implicit */ StateTimeInvariant(const char* str);
    /* implicit */ StateTimeInvariant(std::string str);
    StateTimeInvariant(std::string clk, TimeInvariantOp oper, std::uint64_t dur, TimeUnit u = TimeUnit::Milliseconds);

    /**
     * @brief Formats the invariant into its canonical expression string.
     */
    [[nodiscard]] std::string to_string() const;

    /**
     * @brief Checks whether the invariant constraint is unset.
     */
    [[nodiscard]] bool empty() const noexcept { return raw_expression.empty() && duration == 0; }

    /* implicit */ operator std::string() const { return to_string(); }

    bool operator==(const StateTimeInvariant& other) const noexcept;
    bool operator==(const char* str) const noexcept;
    bool operator==(const std::string& str) const noexcept;
    bool operator==(std::string_view str) const noexcept;

    friend bool operator==(const char* lhs, const StateTimeInvariant& rhs) noexcept { return rhs == lhs; }

    friend bool operator==(const std::string& lhs, const StateTimeInvariant& rhs) noexcept { return rhs == lhs; }

    friend std::ostream& operator<<(std::ostream& os, const StateTimeInvariant& inv);
    friend std::string operator+(const std::string& lhs, const StateTimeInvariant& rhs);
    friend std::string operator+(const char* lhs, const StateTimeInvariant& rhs);
    friend std::string operator+(const StateTimeInvariant& lhs, const std::string& rhs);
    friend std::string operator+(const StateTimeInvariant& lhs, const char* rhs);

  private:
    void parse_from_string(std::string_view str);
    void parse_duration_and_unit(std::string_view dur_str);
};

/**
 * @brief Node representation of a state within the hierarchical state graph.
 */
struct StateNode {
    std::string id;                         ///< Deterministic FNV-1a unique hash
    std::string name;                       ///< Local state identifier
    std::string alias;                      ///< Optional human display alias
    std::string fqn;                        ///< Fully qualified hierarchical path (e.g. "Operating.Running.Manual")
    StateKind kind{StateKind::Atomic};      ///< Structural state classification
    std::string parent_state;               ///< Immediate parent state name
    std::optional<std::string> parent_id;   ///< Parent state deterministic ID
    std::vector<std::string> children_ids;  ///< Ordered IDs of nested child states
    std::vector<OrthogonalRegion> orthogonal_regions;  ///< Parallel regions for orthogonal execution
    std::optional<SubmachineRef> submachine;           ///< Reusable sub-machine statechart invocation

    bool is_composite{false};       ///< True if state encapsulates substates
    bool pinned_parent{false};      ///< When true, normalize_hierarchy must not clear
                                    ///<   parent_state (set by OrthogonalProductPass for
                                    ///<   synthesised Cartesian product states).
    std::string initial_sub_state;  ///< Default sub-state on hierarchical entry
    bool has_history{false};        ///< True if shallow history pseudo-state [H] is active
    bool has_deep_history{false};   ///< True if deep history pseudo-state [H*] is active

    std::vector<ActionSignature> entry_actions;  ///< Ordered entry action signatures
    std::vector<ActionSignature> exit_actions;   ///< Ordered exit action signatures
    std::optional<std::string> do_activity;      ///< Async background activity (e.g., coroutine/worker)

    std::vector<uint32_t> outgoing_transitions;  ///< Contiguous indices into FsmIr::transitions (O(1) graph traversal)
    std::vector<uint32_t> incoming_transitions;  ///< Contiguous indices into FsmIr::transitions (O(1) graph traversal)

    std::optional<StateTimeInvariant>
        time_invariant;                    ///< Timed Automata permanence constraint (e.g., "stay_duration <= 500ms")
    std::vector<GuardAstNode> invariants;  ///< Formal multi-clock and data-path permanence invariants
    std::vector<std::string> deferred_events;    ///< Events deferred while in this state
    std::vector<std::string> traceability_reqs;  ///< Traceability requirement tags (e.g., "REQ-SAFETY-01")
    std::string description;                     ///< Human-readable documentation comment

    StateNode() = default;

    explicit StateNode(std::string state_name, std::string state_desc = "", std::string parent = "")
        : name(std::move(state_name)), parent_state(std::move(parent)), description(std::move(state_desc)) {
        if (!name.empty()) {
            id = compute_deterministic_id(name);
            fqn = parent_state.empty() ? name : (parent_state + "." + name);
        }
    }

    StateNode(std::string state_id, std::string state_name, std::string state_fqn, StateKind state_kind)
        : id(std::move(state_id)), name(std::move(state_name)), fqn(std::move(state_fqn)), kind(state_kind) {}

    /**
     * @brief Retrieves the primary entry action name.
     */
    [[nodiscard]] std::string get_entry_action() const {
        return entry_actions.empty() ? "" : entry_actions.front().name;
    }

    /**
     * @brief Retrieves the primary exit action name.
     */
    [[nodiscard]] std::string get_exit_action() const { return exit_actions.empty() ? "" : exit_actions.front().name; }

    /**
     * @brief Replaces entry actions with a single named action invocation.
     */
    void set_entry_action(std::string act) {
        entry_actions.clear();
        if (!act.empty()) {
            entry_actions.emplace_back(act, act);
        }
    }

    /**
     * @brief Replaces exit actions with a single named action invocation.
     */
    void set_exit_action(std::string act) {
        exit_actions.clear();
        if (!act.empty()) {
            exit_actions.emplace_back(act, act);
        }
    }

    bool operator<(const StateNode& other) const noexcept { return name < other.name; }

    bool operator==(const StateNode& other) const noexcept {
        return id == other.id && name == other.name && fqn == other.fqn && kind == other.kind &&
               parent_id == other.parent_id && children_ids == other.children_ids &&
               orthogonal_regions == other.orthogonal_regions && submachine == other.submachine &&
               entry_actions == other.entry_actions && exit_actions == other.exit_actions &&
               do_activity == other.do_activity && time_invariant == other.time_invariant &&
               deferred_events == other.deferred_events && traceability_reqs == other.traceability_reqs &&
               description == other.description && outgoing_transitions == other.outgoing_transitions &&
               incoming_transitions == other.incoming_transitions;
    }
};

}  // namespace fsm::ir

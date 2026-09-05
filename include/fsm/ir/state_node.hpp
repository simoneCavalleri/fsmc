#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fsm/ir/action.hpp"
#include "fsm/ir/deterministic_id.hpp"
#include "fsm/ir/region.hpp"
#include "fsm/ir/state_kind.hpp"

namespace fsm::ir {

// ============================================================================
// StateNode in the Formal IR
// ============================================================================

/**
 * @brief Node representation of a state within the hierarchical state graph.
 */
struct StateNode {
    std::string id;                         ///< Deterministic FNV-1a unique hash
    std::string name;                       ///< Local state identifier
    std::string alias;                      ///< Optional display alias
    std::string fqn;                        ///< Fully qualified hierarchical path (e.g. "Operating.Running.Manual")
    StateKind kind{StateKind::Atomic};      ///< Structural state classification
    std::string parent_state;               ///< Immediate parent state name
    std::optional<std::string> parent_id;   ///< Parent state deterministic ID
    std::vector<std::string> children_ids;  ///< Ordered IDs of sub-states
    std::vector<OrthogonalRegion> orthogonal_regions;  ///< Parallel regions for orthogonal execution
    std::optional<SubmachineRef> submachine;           ///< Reusable sub-machine statechart invocation

    bool is_composite{false};
    std::string initial_sub_state;  ///< Default sub-state on hierarchical entry
    bool has_history{false};        ///< Shallow history pseudo-state [H]
    bool has_deep_history{false};   ///< Deep history pseudo-state [H*]

    std::vector<ActionSignature> entry_actions;  ///< Ordered entry action signatures
    std::vector<ActionSignature> exit_actions;   ///< Ordered exit action signatures
    std::optional<std::string> do_activity;      ///< Async background activity (e.g., coroutine/worker)

    std::vector<uint32_t> outgoing_transitions;  ///< Contiguous indices into FsmIr::transitions (O(1) graph traversal)
    std::vector<uint32_t> incoming_transitions;  ///< Contiguous indices into FsmIr::transitions (O(1) graph traversal)

    std::optional<std::string>
        time_invariant;  ///< Timed Automata permanence constraint (e.g., "stay_duration <= 500ms")
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

    [[nodiscard]] std::string get_entry_action() const {
        return entry_actions.empty() ? "" : entry_actions.front().name;
    }
    [[nodiscard]] std::string get_exit_action() const {
        return exit_actions.empty() ? "" : exit_actions.front().name;
    }
    void set_entry_action(std::string act) {
        entry_actions.clear();
        if (!act.empty()) {
            entry_actions.emplace_back(act, act);
        }
    }
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
               description == other.description &&
               outgoing_transitions == other.outgoing_transitions &&
               incoming_transitions == other.incoming_transitions;
    }
};

}  // namespace fsm::ir

namespace fsm {
using StateNode = ir::StateNode;
}


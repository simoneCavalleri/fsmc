#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fsm::codegen {

enum class TransitionKind : std::uint8_t { External, Internal, Local };

struct StateModel {
    std::string name;
    std::string alias;
    std::string description;
    std::string parent_state;
    bool is_composite{false};
    std::string initial_sub_state;
    bool has_history{false};
    bool has_deep_history{false};
    std::vector<std::string> deferred_events;
    std::optional<std::string> entry_action;
    std::optional<std::string> exit_action;

    explicit StateModel(std::string state_name = "", std::string state_desc = "", std::string parent = "")
        : name(std::move(state_name)), description(std::move(state_desc)), parent_state(std::move(parent)) {}

    bool operator<(const StateModel& other) const noexcept { return name < other.name; }
};

struct EventModel {
    std::string name;
    std::string description;

    explicit EventModel(std::string event_name = "", std::string event_desc = "")
        : name(std::move(event_name)), description(std::move(event_desc)) {}

    bool operator<(const EventModel& other) const noexcept { return name < other.name; }
};

struct GuardModel {
    std::string name;
    std::string description;

    explicit GuardModel(std::string guard_name = "", std::string guard_desc = "")
        : name(std::move(guard_name)), description(std::move(guard_desc)) {}

    bool operator<(const GuardModel& other) const noexcept { return name < other.name; }
};

struct ActionModel {
    std::string name;
    std::string description;

    explicit ActionModel(std::string action_name = "", std::string action_desc = "")
        : name(std::move(action_name)), description(std::move(action_desc)) {}

    bool operator<(const ActionModel& other) const noexcept { return name < other.name; }
};

struct TransitionModel {
    std::string source;
    std::string target;
    std::string event;
    std::optional<std::string> guard;
    std::optional<std::string> action;
    std::string description;
    TransitionKind kind{TransitionKind::External};
    bool target_is_history{false};
    bool target_is_deep_history{false};
    std::string parent_scope;

    TransitionModel() = default;
    TransitionModel(std::string src, std::string dst, std::string evt, std::optional<std::string> grd = std::nullopt,
                    std::optional<std::string> act = std::nullopt, std::string desc = "",
                    TransitionKind transition_kind = TransitionKind::External)
        : source(std::move(src)),
          target(std::move(dst)),
          event(std::move(evt)),
          guard(std::move(grd)),
          action(std::move(act)),
          description(std::move(desc)),
          kind(transition_kind) {}
};

struct ChoiceNodeModel {
    std::string name;
    std::vector<TransitionModel> outgoing_branches;

    explicit ChoiceNodeModel(std::string choice_name = "") : name(std::move(choice_name)) {}
};

struct FsmModel {
    std::string name = "MyStateMachine";
    std::string ns = "fsm_generated";
    std::string context_type = "no_context";
    std::string initial_state;
    bool thread_safe = true;

    std::vector<StateModel> states;
    std::vector<EventModel> events;
    std::vector<GuardModel> guards;
    std::vector<ActionModel> actions;
    std::vector<TransitionModel> transitions;
    std::vector<ChoiceNodeModel> choice_nodes;

    void add_state(const std::string& state_name, const std::string& parent = "") {
        if (state_name.empty() || state_name == "[*]") {
            return;
        }
        for (auto& state_item : states) {
            if (state_item.name == state_name) {
                if (!parent.empty() && state_item.parent_state.empty()) {
                    state_item.parent_state = parent;
                }
                return;
            }
        }
        states.emplace_back(state_name, "", parent);
    }

    void add_event(const std::string& event_name) {
        if (event_name.empty()) {
            return;
        }
        for (const auto& event_item : events) {
            if (event_item.name == event_name) {
                return;
            }
        }
        events.emplace_back(event_name);
    }

    void add_guard(const std::string& guard_name) {
        if (guard_name.empty()) {
            return;
        }
        for (const auto& guard_item : guards) {
            if (guard_item.name == guard_name) {
                return;
            }
        }
        guards.emplace_back(guard_name);
    }

    void add_action(const std::string& action_name) {
        if (action_name.empty()) {
            return;
        }
        for (const auto& action_item : actions) {
            if (action_item.name == action_name) {
                return;
            }
        }
        actions.emplace_back(action_name);
    }

    void add_transition(TransitionModel transition_item) { transitions.push_back(std::move(transition_item)); }

    void add_choice_node(const std::string& choice_name) {
        if (choice_name.empty()) {
            return;
        }
        for (const auto& choice_item : choice_nodes) {
            if (choice_item.name == choice_name) {
                return;
            }
        }
        choice_nodes.emplace_back(choice_name);
    }

    [[nodiscard]] bool is_choice_node(const std::string& node_name) const noexcept {
        for (const auto& choice_item : choice_nodes) {
            if (choice_item.name == node_name) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] const StateModel* find_state(const std::string& state_name) const noexcept {
        for (const auto& state_item : states) {
            if (state_item.name == state_name) {
                return &state_item;
            }
        }
        return nullptr;
    }

    [[nodiscard]] StateModel* find_state_mut(const std::string& state_name) noexcept {
        for (auto& state_item : states) {
            if (state_item.name == state_name) {
                return &state_item;
            }
        }
        return nullptr;
    }

    void normalize_hierarchy() {
        if (!initial_state.empty()) {
            if (auto* init = find_state_mut(initial_state)) {
                init->parent_state = "";
            }
        }

        // Find states with outgoing transitions at the root scope
        for (const auto& t : transitions) {
            if (t.parent_scope.empty() && !t.source.empty()) {
                if (auto* src_state = find_state_mut(t.source)) {
                    src_state->parent_state = "";
                }
            }
        }

        // Recompute is_composite for all states
        for (auto& p : states) {
            bool has_children = false;
            for (const auto& c : states) {
                if (c.parent_state == p.name) {
                    has_children = true;
                    break;
                }
            }
            p.is_composite = has_children;
        }
    }
};

}  // namespace fsm::codegen

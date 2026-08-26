#pragma once

#include <chrono>
#include <cstdint>

#include "fsm/runtime/cpp/type_traits.hpp"

namespace fsm {

// Default no-op action
struct no_action {
    constexpr void operator()() const noexcept {}
};

// Default always-true guard
struct no_guard {
    constexpr bool operator()() const noexcept { return true; }
};

// Anonymous / Eventless Transition Trigger Event
struct anonymous_event {};
using completion_event = anonymous_event;

// Timed Transition Event Types
template <typename Duration = std::chrono::milliseconds>
struct after {
    Duration duration{};
    constexpr explicit after(Duration d = Duration{0}) : duration(d) {}
};

template <std::int64_t Milliseconds>
struct after_ms {
    static constexpr std::chrono::milliseconds duration{Milliseconds};
};

// Logical NOT guard combinator
template <typename Guard>
struct not_ {
    constexpr not_() = default;

    template <typename Event, typename SrcState, typename Context, typename... Extra>
    constexpr bool operator()(const Event& evt, const SrcState& src, Context& ctx, const Extra&... extra) const {
        return !call_guard(Guard{}, evt, src, ctx, extra...);
    }
};

// Logical AND guard combinator
template <typename Guard1, typename Guard2, typename... Rest>
struct and_ {
    constexpr and_() = default;

    template <typename Event, typename SrcState, typename Context, typename... Extra>
    constexpr bool operator()(const Event& evt, const SrcState& src, Context& ctx, const Extra&... extra) const {
        if (!call_guard(Guard1{}, evt, src, ctx, extra...)) {
            return false;
        }
        if constexpr (sizeof...(Rest) == 0) {
            return call_guard(Guard2{}, evt, src, ctx, extra...);
        } else {
            return and_<Guard2, Rest...>{}(evt, src, ctx, extra...);
        }
    }
};

// Logical OR guard combinator
template <typename Guard1, typename Guard2, typename... Rest>
struct or_ {
    constexpr or_() = default;

    template <typename Event, typename SrcState, typename Context, typename... Extra>
    constexpr bool operator()(const Event& evt, const SrcState& src, Context& ctx, const Extra&... extra) const {
        if (call_guard(Guard1{}, evt, src, ctx, extra...)) {
            return true;
        }
        if constexpr (sizeof...(Rest) == 0) {
            return call_guard(Guard2{}, evt, src, ctx, extra...);
        } else {
            return or_<Guard2, Rest...>{}(evt, src, ctx, extra...);
        }
    }
};

// History check guard for shallow / deep history transitions
template <typename ParentState, typename SubState>
struct history_is {
    template <typename Event, typename State, typename Context, typename Fsm>
    constexpr bool operator()(const Event& /*evt*/, const State& /*state*/, Context& /*ctx*/, const Fsm& fsm) const {
        return fsm.get_history(ParentState::name) == SubState::name;
    }

    template <typename Event, typename State, typename Context>
    constexpr bool operator()(const Event& /*evt*/, const State& /*state*/, Context& /*ctx*/) const {
        return false;
    }
};

// Single transition specification in compile-time transition table
template <typename SourceState, typename EventType, typename TargetState, typename ActionType = no_action,
          typename GuardType = no_guard>
struct transition {
    using source = SourceState;
    using event = EventType;
    using target = TargetState;
    using action_type = ActionType;
    using guard_type = GuardType;
    static constexpr bool is_internal = false;

    constexpr transition() = default;
};

// Internal transition specification (executes action without exiting/entering state)
template <typename State, typename EventType, typename ActionType = no_action, typename GuardType = no_guard>
struct internal_transition : transition<State, EventType, State, ActionType, GuardType> {
    static constexpr bool is_internal = true;
    constexpr internal_transition() = default;
};

// ============================================================================
// Fluent Transition Builders: row, on, and internal_row
// ============================================================================

// Fluent Transition Builder: row<Src, Event, Dst>::when<Guard>::then<Action>
template <typename SourceState, typename EventType, typename TargetState, typename ActionType = no_action,
          typename GuardType = no_guard>
struct row : transition<SourceState, EventType, TargetState, ActionType, GuardType> {
    using base = transition<SourceState, EventType, TargetState, ActionType, GuardType>;

    constexpr row() = default;

    // Add / override Guard
    template <typename NewGuard>
    using guard = row<SourceState, EventType, TargetState, ActionType, NewGuard>;

    template <typename NewGuard>
    using when = row<SourceState, EventType, TargetState, ActionType, NewGuard>;

    // Add / override Action
    template <typename NewAction>
    using action = row<SourceState, EventType, TargetState, NewAction, GuardType>;

    template <typename NewAction>
    using then = row<SourceState, EventType, TargetState, NewAction, GuardType>;
};

// Fluent Internal Transition Builder: internal_row<State, Event>::when<Guard>::then<Action>
template <typename State, typename EventType, typename ActionType = no_action, typename GuardType = no_guard>
struct internal_row : internal_transition<State, EventType, ActionType, GuardType> {
    using base = internal_transition<State, EventType, ActionType, GuardType>;

    constexpr internal_row() = default;

    template <typename NewGuard>
    using guard = internal_row<State, EventType, ActionType, NewGuard>;

    template <typename NewGuard>
    using when = internal_row<State, EventType, ActionType, NewGuard>;

    template <typename NewAction>
    using action = internal_row<State, EventType, NewAction, GuardType>;

    template <typename NewAction>
    using then = internal_row<State, EventType, NewAction, GuardType>;
};

// Fluent Event-First Builder: on<Event, Source>::to<Target>::when<Guard>::then<Action>
template <typename EventType, typename SourceState>
struct on {
    template <typename TargetState>
    using to = row<SourceState, EventType, TargetState>;
};

// Helper for fluent creation
template <typename SourceState, typename EventType, typename TargetState, typename ActionType = no_action,
          typename GuardType = no_guard>
constexpr auto make_transition(ActionType action = {}, GuardType guard = {}) {
    return transition<SourceState, EventType, TargetState, ActionType, GuardType>{std::move(action), std::move(guard)};
}

}  // namespace fsm

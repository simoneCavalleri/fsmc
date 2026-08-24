#pragma once

#include <chrono>
#include <cstdint>

#include "type_traits.hpp"

namespace fsm {

// Default no-op action
struct no_action {
    constexpr void operator()() const noexcept {}
};

// Default always-true guard
struct no_guard {
    constexpr bool operator()() const noexcept { return true; }
};

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
    Guard guard_fn{};

    constexpr not_() = default;
    constexpr explicit not_(Guard g) : guard_fn(std::move(g)) {}

    template <typename Event, typename SrcState, typename Context, typename... Extra>
    constexpr bool operator()(const Event& evt, const SrcState& src, Context& ctx, const Extra&... extra) const {
        return !call_guard(guard_fn, evt, src, ctx, extra...);
    }
};

// Logical AND guard combinator
template <typename Guard1, typename Guard2, typename... Rest>
struct and_ {
    Guard1 g1{};
    Guard2 g2{};

    constexpr and_() = default;
    constexpr and_(Guard1 first, Guard2 second) : g1(std::move(first)), g2(std::move(second)) {}

    template <typename Event, typename SrcState, typename Context, typename... Extra>
    constexpr bool operator()(const Event& evt, const SrcState& src, Context& ctx, const Extra&... extra) const {
        if (!call_guard(g1, evt, src, ctx, extra...)) {
            return false;
        }
        if constexpr (sizeof...(Rest) == 0) {
            return call_guard(g2, evt, src, ctx, extra...);
        } else {
            return and_<Guard2, Rest...>{}(evt, src, ctx, extra...);
        }
    }
};

// Logical OR guard combinator
template <typename Guard1, typename Guard2, typename... Rest>
struct or_ {
    Guard1 g1{};
    Guard2 g2{};

    constexpr or_() = default;
    constexpr or_(Guard1 first, Guard2 second) : g1(std::move(first)), g2(std::move(second)) {}

    template <typename Event, typename SrcState, typename Context, typename... Extra>
    constexpr bool operator()(const Event& evt, const SrcState& src, Context& ctx, const Extra&... extra) const {
        if (call_guard(g1, evt, src, ctx, extra...)) {
            return true;
        }
        if constexpr (sizeof...(Rest) == 0) {
            return call_guard(g2, evt, src, ctx, extra...);
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

    ActionType action_fn{};
    GuardType guard_fn{};

    constexpr transition() = default;
    constexpr transition(ActionType act, GuardType grd = {}) : action_fn(std::move(act)), guard_fn(std::move(grd)) {}
};

// Internal transition specification (executes action without exiting/entering state)
template <typename State, typename EventType, typename ActionType = no_action, typename GuardType = no_guard>
struct internal_transition : transition<State, EventType, State, ActionType, GuardType> {
    static constexpr bool is_internal = true;
    using transition<State, EventType, State, ActionType, GuardType>::transition;
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
    constexpr row(ActionType act, GuardType grd = {}) : base(std::move(act), std::move(grd)) {}

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
    constexpr internal_row(ActionType act, GuardType grd = {}) : base(std::move(act), std::move(grd)) {}

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

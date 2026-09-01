#pragma once

#include <chrono>
#include <cstdint>

#include "fsm/backend/cpp/runtime/type_traits.hpp"

namespace fsm {

/**
 * @brief Default no-op action functor.
 */
struct no_action {
    constexpr void operator()() const noexcept {}
};

/**
 * @brief Default always-true guard functor.
 */
struct no_guard {
    constexpr bool operator()() const noexcept { return true; }
};

/**
 * @brief Anonymous / Eventless Transition Trigger Event.
 *
 * Used for spontaneous continuous transitions evaluated during `step()`.
 */
struct anonymous_event {};
using completion_event = anonymous_event;

/**
 * @brief Timed Transition Trigger (elapsed duration for Asynchronous Reactive Dispatch).
 *
 * Used for wall-clock / steady_clock timeout events posted to the async event queue.
 */
template <typename Duration = std::chrono::milliseconds>
struct after {
    Duration duration{};
    constexpr explicit after(Duration d = Duration{0}) : duration(d) {}
};

/**
 * @brief Compile-time millisecond timed transition trigger for Asynchronous Reactive Dispatch.
 */
template <std::int64_t Milliseconds>
struct after_ms {
    static constexpr std::chrono::milliseconds duration{Milliseconds};
};

namespace detail {
template <typename T, typename = void>
struct has_elapsed_ticks : std::false_type {};
template <typename T>
struct has_elapsed_ticks<T, std::void_t<decltype(std::declval<T>().elapsed_ticks)>> : std::true_type {};

template <typename T, typename = void>
struct has_state_time_ms : std::false_type {};
template <typename T>
struct has_state_time_ms<T, std::void_t<decltype(std::declval<T>().state_time_ms)>> : std::true_type {};

template <typename T, typename = void>
struct has_state_elapsed_time : std::false_type {};
template <typename T>
struct has_state_elapsed_time<T, std::void_t<decltype(std::declval<T>().state_elapsed_time)>> : std::true_type {};
}  // namespace detail

/**
 * @brief Discrete Time In-State Residence Guard (for Sampled Synchronous Control Loops).
 *
 * Checks if the state residence counter (stored deterministically in Registers z^-1)
 * has reached or exceeded the specified threshold. Zero heap, zero thread, SMT verifiable.
 *
 * @tparam Threshold Value comparison threshold (ticks or time units).
 */
template <auto Threshold>
struct in_state_for {
    template <typename... Args>
    constexpr bool operator()(const Args&... args) const noexcept {
        return evaluate(args...);
    }

  private:
    template <typename First, typename... Rest>
    static constexpr bool evaluate(const First& first, const Rest&... rest) noexcept {
        if constexpr (detail::has_elapsed_ticks<First>::value) {
            return first.elapsed_ticks >= Threshold;
        } else if constexpr (detail::has_state_time_ms<First>::value) {
            return first.state_time_ms >= Threshold;
        } else if constexpr (detail::has_state_elapsed_time<First>::value) {
            return first.state_elapsed_time >= Threshold;
        } else if constexpr (sizeof...(Rest) > 0) {
            return evaluate(rest...);
        } else {
            return true;
        }
    }

    static constexpr bool evaluate() noexcept { return true; }
};

/**
 * @brief Logical NOT guard combinator: `!Guard(args...)`.
 * @tparam Guard The underlying atomic or composite guard type to invert.
 */
template <typename Guard>
struct not_ {
    constexpr not_() = default;

    template <typename... Args>
    constexpr bool operator()(const Args&... args) const {
        return !call_guard(Guard{}, args...);
    }
};

/**
 * @brief Variadic Logical AND guard combinator: `Guard1 && Guard2 && ... && Rest`.
 * Short-circuits evaluation on the first false guard.
 */
template <typename Guard1, typename Guard2, typename... Rest>
struct and_ {
    constexpr and_() = default;

    template <typename... Args>
    constexpr bool operator()(const Args&... args) const {
        if (!call_guard(Guard1{}, args...)) {
            return false;
        }
        if constexpr (sizeof...(Rest) == 0) {
            return call_guard(Guard2{}, args...);
        } else {
            return and_<Guard2, Rest...>{}(args...);
        }
    }
};

/**
 * @brief Variadic Logical OR guard combinator: `Guard1 || Guard2 || ... || Rest`.
 * Short-circuits evaluation on the first true guard.
 */
template <typename Guard1, typename Guard2, typename... Rest>
struct or_ {
    constexpr or_() = default;

    template <typename... Args>
    constexpr bool operator()(const Args&... args) const {
        if (call_guard(Guard1{}, args...)) {
            return true;
        }
        if constexpr (sizeof...(Rest) == 0) {
            return call_guard(Guard2{}, args...);
        } else {
            return or_<Guard2, Rest...>{}(args...);
        }
    }
};

/**
 * @brief History state predicate guard for UML 2.5 Shallow and Deep History transitions.
 * @tparam ParentState The composite parent state maintaining the history tracker.
 * @tparam SubState The recorded active substate to compare against.
 *
 * The generated dispatcher (fsm::try_transition_from_ports) invokes a guard as
 * `call_guard(Guard{}, event, src_state, in, registers_, srv, *this)` — six arguments.
 * Only the six-argument form (selected by call_guard's is_invocable_r_v<bool, Guard, Args...>
 * fast path) is provided.
 */
template <typename ParentState, typename SubState>
struct history_is {
    template <typename Event, typename State, typename InPorts, typename Registers, typename Services, typename Fsm>
    constexpr auto operator()(const Event& /*evt*/, const State& /*state*/, const InPorts& /*in*/,
                              const Registers& /*reg*/, Services& /*srv*/, const Fsm& fsm) const
        -> decltype(fsm.get_history(ParentState::name) == SubState::name) {
        return fsm.get_history(ParentState::name) == SubState::name;
    }
};

/**
 * @brief Canonical representation of an external state transition in a compile-time table.
 *
 * @tparam SourceState The source state type.
 * @tparam EventType The event trigger type.
 * @tparam TargetState The destination state type.
 * @tparam ActionType The action functor type executed during transition.
 * @tparam GuardType The guard functor predicate.
 */
template <typename SourceState, typename EventType, typename TargetState, typename ActionType = no_action,
          typename GuardType = no_guard>
struct transition {
    static_assert(!std::is_fundamental_v<GuardType> || std::is_same_v<GuardType, no_guard>,
                  "GuardType must be a callable functor class or fsm::no_guard, not a primitive scalar type.");
    static_assert(!std::is_fundamental_v<ActionType> || std::is_same_v<ActionType, no_action>,
                  "ActionType must be a callable functor class or fsm::no_action, not a primitive scalar type.");

    using source = SourceState;
    using event = EventType;
    using target = TargetState;
    using action_type = ActionType;
    using guard_type = GuardType;
    static constexpr bool is_internal = false;

    constexpr transition() = default;
};

/**
 * @brief Canonical representation of an internal state transition (no state exit/entry lifecycle).
 */
template <typename State, typename EventType, typename ActionType = no_action, typename GuardType = no_guard>
struct internal_transition : transition<State, EventType, State, ActionType, GuardType> {
    static constexpr bool is_internal = true;
    constexpr internal_transition() = default;
};

// ============================================================================
// Fluent Transition Builders: row, on, and internal_row
// ============================================================================

/**
 * @brief Fluent transition row builder for type-safe static table definitions.
 *
 * Example:
 * ```cpp
 * using Table = ::fsm::transition_table<
 *     ::fsm::row<Idle, StartCmd, Running>::when<HasPower>::then<StartMotor>,
 *     ::fsm::row<Running, StopCmd, Idle>::then<StopMotor>
 * >;
 * ```
 */
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

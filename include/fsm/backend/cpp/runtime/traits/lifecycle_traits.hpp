#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fsm {

// ============================================================================
// State Lifecycle Detection & Invocations (on_enter, on_exit)
// Partitioned Domain Model: InPorts (const &), OutPorts (&), Registers (&), Services (&)
// ============================================================================

namespace detail {

// on_enter(event, in, out, reg, srv)
template <typename State, typename Event, typename InPorts, typename OutPorts, typename Registers, typename Services,
          typename = void>
struct has_on_enter_full : std::false_type {};

template <typename State, typename Event, typename InPorts, typename OutPorts, typename Registers, typename Services>
struct has_on_enter_full<State, Event, InPorts, OutPorts, Registers, Services,
                         std::void_t<decltype(std::declval<State&>().on_enter(
                             std::declval<const Event&>(), std::declval<const InPorts&>(), std::declval<OutPorts&>(),
                             std::declval<Registers&>(), std::declval<Services&>()))>> : std::true_type {};

// on_enter(event)
template <typename State, typename Event, typename = void>
struct has_on_enter_event : std::false_type {};

template <typename State, typename Event>
struct has_on_enter_event<State, Event,
                          std::void_t<decltype(std::declval<State&>().on_enter(std::declval<const Event&>()))>>
    : std::true_type {};

// on_enter(in, out, reg, srv)
template <typename State, typename InPorts, typename OutPorts, typename Registers, typename Services, typename = void>
struct has_on_enter_ports : std::false_type {};

template <typename State, typename InPorts, typename OutPorts, typename Registers, typename Services>
struct has_on_enter_ports<
    State, InPorts, OutPorts, Registers, Services,
    std::void_t<decltype(std::declval<State&>().on_enter(std::declval<const InPorts&>(), std::declval<OutPorts&>(),
                                                         std::declval<Registers&>(), std::declval<Services&>()))>>
    : std::true_type {};

// on_enter()
template <typename State, typename = void>
struct has_on_enter_void : std::false_type {};

template <typename State>
struct has_on_enter_void<State, std::void_t<decltype(std::declval<State&>().on_enter())>> : std::true_type {};

// on_exit(event, in, out, reg, srv)
template <typename State, typename Event, typename InPorts, typename OutPorts, typename Registers, typename Services,
          typename = void>
struct has_on_exit_full : std::false_type {};

template <typename State, typename Event, typename InPorts, typename OutPorts, typename Registers, typename Services>
struct has_on_exit_full<State, Event, InPorts, OutPorts, Registers, Services,
                        std::void_t<decltype(std::declval<State&>().on_exit(
                            std::declval<const Event&>(), std::declval<const InPorts&>(), std::declval<OutPorts&>(),
                            std::declval<Registers&>(), std::declval<Services&>()))>> : std::true_type {};

// on_exit(event)
template <typename State, typename Event, typename = void>
struct has_on_exit_event : std::false_type {};

template <typename State, typename Event>
struct has_on_exit_event<State, Event,
                         std::void_t<decltype(std::declval<State&>().on_exit(std::declval<const Event&>()))>>
    : std::true_type {};

// on_exit(in, out, reg, srv)
template <typename State, typename InPorts, typename OutPorts, typename Registers, typename Services, typename = void>
struct has_on_exit_ports : std::false_type {};

template <typename State, typename InPorts, typename OutPorts, typename Registers, typename Services>
struct has_on_exit_ports<
    State, InPorts, OutPorts, Registers, Services,
    std::void_t<decltype(std::declval<State&>().on_exit(std::declval<const InPorts&>(), std::declval<OutPorts&>(),
                                                        std::declval<Registers&>(), std::declval<Services&>()))>>
    : std::true_type {};

// on_exit()
template <typename State, typename = void>
struct has_on_exit_void : std::false_type {};

template <typename State>
struct has_on_exit_void<State, std::void_t<decltype(std::declval<State&>().on_exit())>> : std::true_type {};

}  // namespace detail

// ----------------------------------------------------------------------------
// Safe invocation of on_enter hook
// ----------------------------------------------------------------------------
template <typename State, typename Event, typename InPorts, typename OutPorts, typename Registers, typename Services>
constexpr void call_on_enter(State& state, const Event& event, const InPorts& in, OutPorts& out, Registers& reg,
                             Services& srv) {
    if constexpr (detail::has_on_enter_full<State, Event, InPorts, OutPorts, Registers, Services>::value) {
        state.on_enter(event, in, out, reg, srv);
    } else if constexpr (detail::has_on_enter_event<State, Event>::value) {
        state.on_enter(event);
    } else if constexpr (detail::has_on_enter_ports<State, InPorts, OutPorts, Registers, Services>::value) {
        state.on_enter(in, out, reg, srv);
    } else if constexpr (detail::has_on_enter_void<State>::value) {
        state.on_enter();
    }
}

template <typename State, typename InPorts, typename OutPorts, typename Registers, typename Services>
constexpr void call_on_enter(State& state, const InPorts& in, OutPorts& out, Registers& reg, Services& srv) {
    if constexpr (detail::has_on_enter_ports<State, InPorts, OutPorts, Registers, Services>::value) {
        state.on_enter(in, out, reg, srv);
    } else if constexpr (detail::has_on_enter_void<State>::value) {
        state.on_enter();
    }
}

template <typename State, typename Event>
constexpr void call_on_enter(State& state, const Event& event) {
    if constexpr (detail::has_on_enter_event<State, Event>::value) {
        state.on_enter(event);
    } else if constexpr (detail::has_on_enter_void<State>::value) {
        state.on_enter();
    }
}

template <typename State>
constexpr void call_on_enter(State& state) {
    if constexpr (detail::has_on_enter_void<State>::value) {
        state.on_enter();
    }
}

// ----------------------------------------------------------------------------
// Safe invocation of on_exit hook
// ----------------------------------------------------------------------------
template <typename State, typename Event, typename InPorts, typename OutPorts, typename Registers, typename Services>
constexpr void call_on_exit(State& state, const Event& event, const InPorts& in, OutPorts& out, Registers& reg,
                            Services& srv) {
    if constexpr (detail::has_on_exit_full<State, Event, InPorts, OutPorts, Registers, Services>::value) {
        state.on_exit(event, in, out, reg, srv);
    } else if constexpr (detail::has_on_exit_event<State, Event>::value) {
        state.on_exit(event);
    } else if constexpr (detail::has_on_exit_ports<State, InPorts, OutPorts, Registers, Services>::value) {
        state.on_exit(in, out, reg, srv);
    } else if constexpr (detail::has_on_exit_void<State>::value) {
        state.on_exit();
    }
}

template <typename State, typename Event>
constexpr void call_on_exit(State& state, const Event& event) {
    if constexpr (detail::has_on_exit_event<State, Event>::value) {
        state.on_exit(event);
    } else if constexpr (detail::has_on_exit_void<State>::value) {
        state.on_exit();
    }
}

template <typename State>
constexpr void call_on_exit(State& state) {
    if constexpr (detail::has_on_exit_void<State>::value) {
        state.on_exit();
    }
}

// ============================================================================
// State Time Invariant / Permanence Bounds
// ============================================================================

namespace detail {

template <typename State, typename = void>
struct state_time_invariant_traits {
    static constexpr bool has_invariant = false;
    static constexpr std::uint64_t max_stay_duration_ms = UINT64_MAX;
};

template <typename State>
struct state_time_invariant_traits<State, std::void_t<decltype(State::max_stay_duration_ms)>> {
    static constexpr bool has_invariant = true;
    static constexpr std::uint64_t max_stay_duration_ms = State::max_stay_duration_ms;
};

}  // namespace detail

template <typename State>
inline constexpr bool has_time_invariant_v = detail::state_time_invariant_traits<State>::has_invariant;

template <typename State>
inline constexpr std::uint64_t state_max_stay_ms_v = detail::state_time_invariant_traits<State>::max_stay_duration_ms;

/**
 * @brief Structured runtime diagnostic information on state time invariant violation.
 */
struct invariant_violation_info {
    std::string_view state_name{};
    std::uint64_t residence_time_ms{0};
    std::uint64_t max_stay_duration_ms{0};

    [[nodiscard]] constexpr bool operator==(const invariant_violation_info& other) const noexcept {
        return state_name == other.state_name && residence_time_ms == other.residence_time_ms &&
               max_stay_duration_ms == other.max_stay_duration_ms;
    }
    [[nodiscard]] constexpr bool operator!=(const invariant_violation_info& other) const noexcept {
        return !(*this == other);
    }
};

}  // namespace fsm

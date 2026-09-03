#pragma once

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

}  // namespace fsm

#pragma once

#include <type_traits>
#include <utility>

namespace fsm {

// ============================================================================
// Detection Idiom & Hook Invocations (on_enter, on_exit, guard, action)
// Partitioned Domain Model: InPorts (const &), OutPorts (&), Registers (&), Services (&)
// ============================================================================

namespace detail {

// on_enter(event, in, out, reg, srv)
template <typename State, typename Event, typename InPorts, typename OutPorts, typename Registers, typename Services,
          typename = void>
struct has_on_enter_full : std::false_type {};

template <typename State, typename Event, typename InPorts, typename OutPorts, typename Registers, typename Services>
struct has_on_enter_full<
    State, Event, InPorts, OutPorts, Registers, Services,
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
    std::void_t<decltype(std::declval<State&>().on_enter(
        std::declval<const InPorts&>(), std::declval<OutPorts&>(),
        std::declval<Registers&>(), std::declval<Services&>()))>> : std::true_type {};

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
struct has_on_exit_full<
    State, Event, InPorts, OutPorts, Registers, Services,
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

template <typename State>
constexpr void call_on_exit(State& state) {
    if constexpr (detail::has_on_exit_void<State>::value) {
        state.on_exit();
    }
}

// ----------------------------------------------------------------------------
// Safe invocation of guard
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Safe invocation of guard and action fallbacks
// ----------------------------------------------------------------------------
namespace detail {

template <std::size_t I, typename Tuple>
constexpr decltype(auto) tuple_get(Tuple& t) {
    if constexpr (I < std::tuple_size_v<std::remove_reference_t<Tuple>>) {
        return std::get<I>(t);
    } else {
        struct empty_val {};
        return empty_val{};
    }
}

// Guard channel tuple layout: (0: evt, 1: src, 2: in, 3: reg, 4: srv, 5: fsm)
template <typename Guard, typename Tuple>
constexpr bool invoke_guard_fallback(const Guard& guard, Tuple& t) {
    constexpr std::size_t N = std::tuple_size_v<std::remove_reference_t<Tuple>>;

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L && __cplusplus >= 202002L
    if constexpr (N >= 6 && requires {
                      { guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t),
                              tuple_get<5>(t)) } -> std::convertible_to<bool>;
                  }) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t),
                     tuple_get<5>(t));
    } else if constexpr (N >= 4 && requires {
                             { guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t),
                                     tuple_get<N - 1>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 3 && requires {
                             { guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<N - 1>(t)) } ->
                                 std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 2 && requires {
                             { guard(tuple_get<0>(t), tuple_get<N - 1>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 1 && requires {
                             { guard(tuple_get<N - 1>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<N - 1>(t));
    } else if constexpr (N >= 5 && requires {
                             { guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t),
                                     tuple_get<4>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 4 && requires {
                             { guard(tuple_get<0>(t), tuple_get<2>(t), tuple_get<3>(t),
                                     tuple_get<4>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 3 && requires {
                             { guard(tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t)) } ->
                                 std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 4 && requires {
                             { guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t),
                                     tuple_get<3>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && requires {
                             { guard(tuple_get<0>(t), tuple_get<2>(t), tuple_get<3>(t)) } ->
                                 std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<2>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && requires {
                             { guard(tuple_get<2>(t), tuple_get<3>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<2>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && requires {
                             { guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<3>(t)) } ->
                                 std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && requires {
                             { guard(tuple_get<0>(t), tuple_get<3>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && requires {
                             { guard(tuple_get<1>(t), tuple_get<3>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<1>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && requires {
                             { guard(tuple_get<3>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<3>(t));
    } else if constexpr (N >= 3 && requires {
                             { guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t)) } ->
                                 std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t));
    } else if constexpr (N >= 3 && requires {
                             { guard(tuple_get<0>(t), tuple_get<2>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<2>(t));
    } else if constexpr (N >= 3 && requires {
                             { guard(tuple_get<2>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<2>(t));
    } else if constexpr (N >= 2 && requires {
                             { guard(tuple_get<0>(t), tuple_get<1>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<1>(t));
    } else if constexpr (N >= 1 && requires {
                             { guard(tuple_get<0>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t));
    } else if constexpr (N >= 2 && requires {
                             { guard(tuple_get<1>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<1>(t));
    } else if constexpr (requires {
                             { guard() } -> std::convertible_to<bool>;
                         }) {
        return guard();
    } else {
        return true;
    }
#else
    if constexpr (N >= 6 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t)), decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t)), decltype(tuple_get<5>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t));
    } else if constexpr (N >= 4 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t)), decltype(tuple_get<N - 1>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 3 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<N - 1>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 2 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<N - 1>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 1 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<N - 1>(t))>) {
        return guard(tuple_get<N - 1>(t));
    } else if constexpr (N >= 5 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t)), decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 4 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<2>(t)), decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 3 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<2>(t)), decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t))>) {
        return guard(tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 4 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t)), decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<2>(t)), decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<2>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<2>(t)), decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<2>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<1>(t)), decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<1>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<3>(t));
    } else if constexpr (N >= 3 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t));
    } else if constexpr (N >= 3 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<2>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<2>(t));
    } else if constexpr (N >= 3 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<2>(t))>) {
        return guard(tuple_get<2>(t));
    } else if constexpr (N >= 2 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t));
    } else if constexpr (N >= 1 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t))>) {
        return guard(tuple_get<0>(t));
    } else if constexpr (N >= 2 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<1>(t))>) {
        return guard(tuple_get<1>(t));
    } else if constexpr (std::is_invocable_r_v<bool, Guard>) {
        return guard();
    } else {
        return true;
    }
#endif
}

// Action channel tuple layout: (0: evt, 1: src, 2: dst, 3: in, 4: out, 5: reg, 6: srv)
template <typename Action, typename Tuple>
constexpr void invoke_action_fallback(Action& action, Tuple& t) {
    constexpr std::size_t N = std::tuple_size_v<std::remove_reference_t<Tuple>>;

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L && __cplusplus >= 202002L
    if constexpr (N >= 7 && requires {
                      action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t),
                             tuple_get<5>(t), tuple_get<6>(t));
                  }) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t),
              tuple_get<6>(t));
    } else if constexpr (N >= 6 && requires {
                             action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<3>(t), tuple_get<4>(t),
                                    tuple_get<5>(t), tuple_get<6>(t));
                         }) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
    } else if constexpr (N >= 5 && requires {
                             action(tuple_get<0>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t),
                                    tuple_get<6>(t));
                         }) {
        action(tuple_get<0>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
    } else if constexpr (N >= 4 && requires {
                             action(tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
                         }) {
        action(tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
    } else if constexpr (N >= 6 && requires {
                             action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<5>(t));
                         }) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && requires { action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<5>(t)); }) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && requires { action(tuple_get<0>(t), tuple_get<2>(t), tuple_get<5>(t)); }) {
        action(tuple_get<0>(t), tuple_get<2>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && requires { action(tuple_get<0>(t), tuple_get<4>(t), tuple_get<5>(t)); }) {
        action(tuple_get<0>(t), tuple_get<4>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && requires { action(tuple_get<4>(t), tuple_get<5>(t)); }) {
        action(tuple_get<4>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && requires { action(tuple_get<0>(t), tuple_get<5>(t)); }) {
        action(tuple_get<0>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && requires { action(tuple_get<1>(t), tuple_get<2>(t), tuple_get<5>(t)); }) {
        action(tuple_get<1>(t), tuple_get<2>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && requires { action(tuple_get<1>(t), tuple_get<5>(t)); }) {
        action(tuple_get<1>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && requires { action(tuple_get<5>(t)); }) {
        action(tuple_get<5>(t));
    } else if constexpr (N >= 7 && requires {
                             action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<6>(t));
                         }) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<6>(t));
    } else if constexpr (N >= 7 && requires { action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<6>(t)); }) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<6>(t));
    } else if constexpr (N >= 7 && requires { action(tuple_get<0>(t), tuple_get<6>(t)); }) {
        action(tuple_get<0>(t), tuple_get<6>(t));
    } else if constexpr (N >= 7 && requires { action(tuple_get<6>(t)); }) {
        action(tuple_get<6>(t));
    } else if constexpr (N >= 5 && requires { action(tuple_get<0>(t), tuple_get<3>(t), tuple_get<4>(t)); }) {
        action(tuple_get<0>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 5 && requires { action(tuple_get<3>(t), tuple_get<4>(t)); }) {
        action(tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 5 && requires { action(tuple_get<0>(t), tuple_get<4>(t)); }) {
        action(tuple_get<0>(t), tuple_get<4>(t));
    } else if constexpr (N >= 5 && requires { action(tuple_get<4>(t)); }) {
        action(tuple_get<4>(t));
    } else if constexpr (N >= 4 && requires { action(tuple_get<0>(t), tuple_get<3>(t)); }) {
        action(tuple_get<0>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && requires { action(tuple_get<3>(t)); }) {
        action(tuple_get<3>(t));
    } else if constexpr (N >= 4 && requires {
                             action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<N - 1>(t));
                         }) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 3 && requires {
                             action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<N - 1>(t));
                         }) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 2 && requires { action(tuple_get<0>(t), tuple_get<N - 1>(t)); }) {
        action(tuple_get<0>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 1 && requires { action(tuple_get<N - 1>(t)); }) {
        action(tuple_get<N - 1>(t));
    } else if constexpr (N >= 3 && requires { action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t)); }) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t));
    } else if constexpr (N >= 2 && requires { action(tuple_get<0>(t), tuple_get<1>(t)); }) {
        action(tuple_get<0>(t), tuple_get<1>(t));
    } else if constexpr (N >= 3 && requires { action(tuple_get<0>(t), tuple_get<2>(t)); }) {
        action(tuple_get<0>(t), tuple_get<2>(t));
    } else if constexpr (N >= 1 && requires { action(tuple_get<0>(t)); }) {
        action(tuple_get<0>(t));
    } else if constexpr (N >= 3 && requires { action(tuple_get<1>(t), tuple_get<2>(t)); }) {
        action(tuple_get<1>(t), tuple_get<2>(t));
    } else if constexpr (N >= 2 && requires { action(tuple_get<1>(t)); }) {
        action(tuple_get<1>(t));
    } else if constexpr (requires { action(); }) {
        action();
    }
#else
    if constexpr (N >= 7 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t)), decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t)), decltype(tuple_get<5>(t)), decltype(tuple_get<6>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t)), decltype(tuple_get<5>(t)), decltype(tuple_get<6>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
    } else if constexpr (N >= 5 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t)), decltype(tuple_get<5>(t)), decltype(tuple_get<6>(t))>) {
        action(tuple_get<0>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
    } else if constexpr (N >= 4 && std::is_invocable_v<Action, decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t)), decltype(tuple_get<5>(t)), decltype(tuple_get<6>(t))>) {
        action(tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t)), decltype(tuple_get<5>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<5>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<2>(t)), decltype(tuple_get<5>(t))>) {
        action(tuple_get<0>(t), tuple_get<2>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<4>(t)), decltype(tuple_get<5>(t))>) {
        action(tuple_get<0>(t), tuple_get<4>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<4>(t)), decltype(tuple_get<5>(t))>) {
        action(tuple_get<4>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<5>(t))>) {
        action(tuple_get<0>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t)), decltype(tuple_get<5>(t))>) {
        action(tuple_get<1>(t), tuple_get<2>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<1>(t)), decltype(tuple_get<5>(t))>) {
        action(tuple_get<1>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<5>(t))>) {
        action(tuple_get<5>(t));
    } else if constexpr (N >= 7 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t)), decltype(tuple_get<6>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<6>(t));
    } else if constexpr (N >= 7 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<6>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<6>(t));
    } else if constexpr (N >= 7 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<6>(t))>) {
        action(tuple_get<0>(t), tuple_get<6>(t));
    } else if constexpr (N >= 7 && std::is_invocable_v<Action, decltype(tuple_get<6>(t))>) {
        action(tuple_get<6>(t));
    } else if constexpr (N >= 5 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t))>) {
        action(tuple_get<0>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 5 && std::is_invocable_v<Action, decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t))>) {
        action(tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 5 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<4>(t))>) {
        action(tuple_get<0>(t), tuple_get<4>(t));
    } else if constexpr (N >= 5 && std::is_invocable_v<Action, decltype(tuple_get<4>(t))>) {
        action(tuple_get<4>(t));
    } else if constexpr (N >= 4 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<3>(t))>) {
        action(tuple_get<0>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && std::is_invocable_v<Action, decltype(tuple_get<3>(t))>) {
        action(tuple_get<3>(t));
    } else if constexpr (N >= 4 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t)), decltype(tuple_get<N - 1>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 3 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<N - 1>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 2 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<N - 1>(t))>) {
        action(tuple_get<0>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 1 && std::is_invocable_v<Action, decltype(tuple_get<N - 1>(t))>) {
        action(tuple_get<N - 1>(t));
    } else if constexpr (N >= 3 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t));
    } else if constexpr (N >= 2 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t));
    } else if constexpr (N >= 3 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<2>(t))>) {
        action(tuple_get<0>(t), tuple_get<2>(t));
    } else if constexpr (N >= 1 && std::is_invocable_v<Action, decltype(tuple_get<0>(t))>) {
        action(tuple_get<0>(t));
    } else if constexpr (N >= 3 && std::is_invocable_v<Action, decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t))>) {
        action(tuple_get<1>(t), tuple_get<2>(t));
    } else if constexpr (N >= 2 && std::is_invocable_v<Action, decltype(tuple_get<1>(t))>) {
        action(tuple_get<1>(t));
    } else if constexpr (std::is_invocable_v<Action>) {
        action();
    }
#endif
}

}  // namespace detail

// ----------------------------------------------------------------------------
// Safe invocation of guard
// ----------------------------------------------------------------------------
template <typename Guard, typename... Args>
constexpr bool call_guard(const Guard& guard, Args&&... args) {
    if constexpr (std::is_invocable_r_v<bool, Guard, Args...>) {
        return guard(std::forward<Args>(args)...);
    } else {
        auto t = std::forward_as_tuple(args...);
        return detail::invoke_guard_fallback(guard, t);
    }
}

// ----------------------------------------------------------------------------
// Safe invocation of action
// ----------------------------------------------------------------------------
template <typename Action, typename... Args>
constexpr void call_action(Action& action, Args&&... args) {
    if constexpr (std::is_invocable_v<Action, Args...>) {
        action(std::forward<Args>(args)...);
    } else {
        auto t = std::forward_as_tuple(args...);
        detail::invoke_action_fallback(action, t);
    }
}

}  // namespace fsm

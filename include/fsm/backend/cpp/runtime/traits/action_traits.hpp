#pragma once

#include <tuple>
#include <type_traits>
#include <utility>

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L && __cplusplus >= 202002L
#include <concepts>
#endif

#include "fsm/backend/cpp/runtime/traits/guard_traits.hpp"

namespace fsm {

// ============================================================================
// Action Detection & Fallback Invocations
// Partitioned Domain Model: InPorts (const &), OutPorts (&), Registers (&), Services (&)
// ============================================================================

namespace detail {

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
                             action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t),
                                    tuple_get<6>(t));
                         }) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
    } else if constexpr (N >= 5 && requires {
                             action(tuple_get<0>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t),
                                    tuple_get<6>(t));
                         }) {
        action(tuple_get<0>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
    } else if constexpr (N >= 4 &&
                         requires { action(tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t)); }) {
        action(tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
    } else if constexpr (N >= 6 &&
                         requires { action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<5>(t)); }) {
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
    } else if constexpr (N >= 7 &&
                         requires { action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<6>(t)); }) {
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
    } else if constexpr (N >= 4 &&
                         requires { action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<N - 1>(t)); }) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 3 && requires { action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<N - 1>(t)); }) {
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
    if constexpr (N >= 7 &&
                  std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)),
                                      decltype(tuple_get<2>(t)), decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t)),
                                      decltype(tuple_get<5>(t)), decltype(tuple_get<6>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t),
               tuple_get<6>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)),
                                                       decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t)),
                                                       decltype(tuple_get<5>(t)), decltype(tuple_get<6>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
    } else if constexpr (N >= 5 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<3>(t)),
                                                       decltype(tuple_get<4>(t)), decltype(tuple_get<5>(t)),
                                                       decltype(tuple_get<6>(t))>) {
        action(tuple_get<0>(t), tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
    } else if constexpr (N >= 4 && std::is_invocable_v<Action, decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t)),
                                                       decltype(tuple_get<5>(t)), decltype(tuple_get<6>(t))>) {
        action(tuple_get<3>(t), tuple_get<4>(t), tuple_get<5>(t), tuple_get<6>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)),
                                                       decltype(tuple_get<2>(t)), decltype(tuple_get<5>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)),
                                                       decltype(tuple_get<5>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<2>(t)),
                                                       decltype(tuple_get<5>(t))>) {
        action(tuple_get<0>(t), tuple_get<2>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<4>(t)),
                                                       decltype(tuple_get<5>(t))>) {
        action(tuple_get<0>(t), tuple_get<4>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<4>(t)), decltype(tuple_get<5>(t))>) {
        action(tuple_get<4>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<5>(t))>) {
        action(tuple_get<0>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t)),
                                                       decltype(tuple_get<5>(t))>) {
        action(tuple_get<1>(t), tuple_get<2>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<1>(t)), decltype(tuple_get<5>(t))>) {
        action(tuple_get<1>(t), tuple_get<5>(t));
    } else if constexpr (N >= 6 && std::is_invocable_v<Action, decltype(tuple_get<5>(t))>) {
        action(tuple_get<5>(t));
    } else if constexpr (N >= 7 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)),
                                                       decltype(tuple_get<2>(t)), decltype(tuple_get<6>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<6>(t));
    } else if constexpr (N >= 7 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)),
                                                       decltype(tuple_get<6>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<6>(t));
    } else if constexpr (N >= 7 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<6>(t))>) {
        action(tuple_get<0>(t), tuple_get<6>(t));
    } else if constexpr (N >= 7 && std::is_invocable_v<Action, decltype(tuple_get<6>(t))>) {
        action(tuple_get<6>(t));
    } else if constexpr (N >= 5 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<3>(t)),
                                                       decltype(tuple_get<4>(t))>) {
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
    } else if constexpr (N >= 4 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)),
                                                       decltype(tuple_get<2>(t)), decltype(tuple_get<N - 1>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 3 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)),
                                                       decltype(tuple_get<N - 1>(t))>) {
        action(tuple_get<0>(t), tuple_get<1>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 2 &&
                         std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<N - 1>(t))>) {
        action(tuple_get<0>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 1 && std::is_invocable_v<Action, decltype(tuple_get<N - 1>(t))>) {
        action(tuple_get<N - 1>(t));
    } else if constexpr (N >= 3 && std::is_invocable_v<Action, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)),
                                                       decltype(tuple_get<2>(t))>) {
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

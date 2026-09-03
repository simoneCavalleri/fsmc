#pragma once

#include <tuple>
#include <type_traits>
#include <utility>

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L && __cplusplus >= 202002L
#include <concepts>
#endif

namespace fsm {

// ============================================================================
// Guard Detection & Fallback Invocations
// Partitioned Domain Model: InPorts (const &), OutPorts (&), Registers (&), Services (&)
// ============================================================================

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
                      {
                          guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t),
                                tuple_get<5>(t))
                      } -> std::convertible_to<bool>;
                  }) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t),
                     tuple_get<5>(t));
    } else if constexpr (N >= 4 && requires {
                             {
                                 guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<N - 1>(t))
                             } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 3 && requires {
                             {
                                 guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<N - 1>(t))
                             } -> std::convertible_to<bool>;
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
                             {
                                 guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t),
                                       tuple_get<4>(t))
                             } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 4 && requires {
                             {
                                 guard(tuple_get<0>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t))
                             } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 3 && requires {
                             { guard(tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 4 && requires {
                             {
                                 guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t))
                             } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && requires {
                             { guard(tuple_get<0>(t), tuple_get<2>(t), tuple_get<3>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<0>(t), tuple_get<2>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && requires {
                             { guard(tuple_get<2>(t), tuple_get<3>(t)) } -> std::convertible_to<bool>;
                         }) {
        return guard(tuple_get<2>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && requires {
                             { guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<3>(t)) } -> std::convertible_to<bool>;
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
                             { guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t)) } -> std::convertible_to<bool>;
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
    if constexpr (N >= 6 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)),
                                                  decltype(tuple_get<2>(t)), decltype(tuple_get<3>(t)),
                                                  decltype(tuple_get<4>(t)), decltype(tuple_get<5>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t),
                     tuple_get<5>(t));
    } else if constexpr (N >= 4 &&
                         std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)),
                                               decltype(tuple_get<2>(t)), decltype(tuple_get<N - 1>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 3 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)),
                                                         decltype(tuple_get<1>(t)), decltype(tuple_get<N - 1>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 2 &&
                         std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<N - 1>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<N - 1>(t));
    } else if constexpr (N >= 1 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<N - 1>(t))>) {
        return guard(tuple_get<N - 1>(t));
    } else if constexpr (N >= 5 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)),
                                                         decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t)),
                                                         decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 4 &&
                         std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<2>(t)),
                                               decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 3 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<2>(t)),
                                                         decltype(tuple_get<3>(t)), decltype(tuple_get<4>(t))>) {
        return guard(tuple_get<2>(t), tuple_get<3>(t), tuple_get<4>(t));
    } else if constexpr (N >= 4 &&
                         std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t)),
                                               decltype(tuple_get<2>(t)), decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)),
                                                         decltype(tuple_get<2>(t)), decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<2>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 &&
                         std::is_invocable_r_v<bool, Guard, decltype(tuple_get<2>(t)), decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<2>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)),
                                                         decltype(tuple_get<1>(t)), decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 &&
                         std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 &&
                         std::is_invocable_r_v<bool, Guard, decltype(tuple_get<1>(t)), decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<1>(t), tuple_get<3>(t));
    } else if constexpr (N >= 4 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<3>(t))>) {
        return guard(tuple_get<3>(t));
    } else if constexpr (N >= 3 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)),
                                                         decltype(tuple_get<1>(t)), decltype(tuple_get<2>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<1>(t), tuple_get<2>(t));
    } else if constexpr (N >= 3 &&
                         std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<2>(t))>) {
        return guard(tuple_get<0>(t), tuple_get<2>(t));
    } else if constexpr (N >= 3 && std::is_invocable_r_v<bool, Guard, decltype(tuple_get<2>(t))>) {
        return guard(tuple_get<2>(t));
    } else if constexpr (N >= 2 &&
                         std::is_invocable_r_v<bool, Guard, decltype(tuple_get<0>(t)), decltype(tuple_get<1>(t))>) {
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

}  // namespace fsm

#pragma once

#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "fsm/runtime/cpp/traits/dispatch_result.hpp"
#include "fsm/runtime/cpp/traits/reflection.hpp"
#include "fsm/runtime/cpp/traits/type_list.hpp"

namespace fsm {

// ============================================================================
// Zero-Overhead Observer Policies & Storage Utilities
// ============================================================================

// Compile-time no-op observer policy (occupies 0 bytes with [[no_unique_address]])
struct no_observer {
    template <typename Info>
    constexpr void operator()(const Info&) const noexcept {}
};

// Dynamic observer policy for runtime lambda / function registration
struct dynamic_observer {
    using callback_type = std::function<void(const transition_info&)>;
    callback_type callback{};

    constexpr dynamic_observer() noexcept = default;
    dynamic_observer(callback_type cb) : callback(std::move(cb)) {}

    template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, dynamic_observer> &&
                                                      !std::is_same_v<std::decay_t<F>, callback_type>>>
    dynamic_observer(F&& cb) : callback(std::forward<F>(cb)) {}

    void operator()(const transition_info& info) const {
        if (callback) {
            callback(info);
        }
    }
};

template <typename T>
struct is_dynamic_observer : std::false_type {};

template <>
struct is_dynamic_observer<dynamic_observer> : std::true_type {};

template <>
struct is_dynamic_observer<std::function<void(const transition_info&)>> : std::true_type {};

template <typename T>
inline constexpr bool is_dynamic_observer_v = is_dynamic_observer<T>::value;

// Empty tag type for non-allocated optional sub-objects
struct empty_storage {};

namespace detail {

template <typename State, typename = void>
struct has_deferred_events : std::false_type {};

template <typename State>
struct has_deferred_events<State, std::void_t<typename State::deferred_events>> : std::true_type {};

template <typename Event, typename List>
struct type_list_contains_event : std::false_type {};

template <typename Event, typename... Ts>
struct type_list_contains_event<Event, type_list<Ts...>> : std::disjunction<std::is_same<Event, Ts>...> {};

template <typename Event, typename... Ts>
struct type_list_contains_event<Event, std::tuple<Ts...>> : std::disjunction<std::is_same<Event, Ts>...> {};

}  // namespace detail

template <typename State>
inline constexpr bool has_deferred_events_v = detail::has_deferred_events<State>::value;

template <typename State, typename Event>
inline constexpr bool is_deferred_event_v = []() constexpr {
    if constexpr (has_deferred_events_v<State>) {
        return detail::type_list_contains_event<std::decay_t<Event>, typename State::deferred_events>::value;
    } else {
        return false;
    }
}();

// Introspection for History & Deferred events across unique state list
template <typename StateList>
struct any_state_has_history : std::false_type {};

template <typename... States>
struct any_state_has_history<type_list<States...>> : std::disjunction<detail::has_parent_name<States>...> {};

template <typename StateList>
struct any_state_has_deferred : std::false_type {};

template <typename... States>
struct any_state_has_deferred<type_list<States...>> : std::disjunction<detail::has_deferred_events<States>...> {};

}  // namespace fsm

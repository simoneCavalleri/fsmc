#pragma once

#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "fsm/backend/cpp/runtime/traits/dispatch_result.hpp"
#include "fsm/backend/cpp/runtime/traits/reflection.hpp"
#include "fsm/backend/cpp/runtime/traits/type_list.hpp"

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
template <typename SubState, typename SuperState, typename = void>
struct is_substate_of_impl : std::false_type {};

template <typename SubState, typename SuperState>
struct is_substate_of_impl<SubState, SuperState, std::void_t<typename SubState::parent_type>> {
    static constexpr bool value = std::is_same_v<typename SubState::parent_type, SuperState> ||
                                  is_substate_of_impl<typename SubState::parent_type, SuperState>::value;
};

template <typename SubState, typename SuperState, typename = void>
struct is_substate_by_name : std::false_type {};

template <typename SubState, typename SuperState>
struct is_substate_by_name<SubState, SuperState, std::void_t<decltype(SubState::parent)>> {
    static constexpr bool value = (get_type_name<SuperState>() == SubState::parent);
};
}  // namespace detail

template <typename SubState, typename SuperState, typename = void>
struct is_substate_of {
    static constexpr bool value = std::is_same_v<SubState, SuperState> ||
                                  detail::is_substate_of_impl<SubState, SuperState>::value ||
                                  detail::is_substate_by_name<SubState, SuperState>::value;
};

template <typename SubState, typename SuperState>
inline constexpr bool is_substate_of_v = is_substate_of<SubState, SuperState>::value;

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
struct count_parent_states : std::integral_constant<std::size_t, 0> {};

template <typename... States>
struct count_parent_states<type_list<States...>>
    : std::integral_constant<std::size_t, (0 + ... + (detail::has_parent_name<States>::value ? 1 : 0))> {};

template <typename StateList>
inline constexpr std::size_t count_parent_states_v = count_parent_states<StateList>::value;

template <typename StateList>
struct any_state_has_deferred : std::false_type {};

template <typename... States>
struct any_state_has_deferred<type_list<States...>> : std::disjunction<detail::has_deferred_events<States>...> {};

}  // namespace fsm

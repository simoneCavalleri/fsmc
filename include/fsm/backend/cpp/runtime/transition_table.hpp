#pragma once

#include "fsm/backend/cpp/runtime/transition.hpp"
#include "fsm/backend/cpp/runtime/type_traits.hpp"

namespace fsm {

namespace detail {

// Helper to extract source and target states from a list of transitions
template <typename... Transitions>
struct extract_states {
    using raw_list =
        type_list_cat_t<type_list<typename Transitions::source...>, type_list<typename Transitions::target...>>;
    using unique_list = type_list_unique_t<raw_list>;
};

// Helper to extract unique events from a list of transitions
template <typename... Transitions>
struct extract_transition_events {
    using raw_list = type_list<typename Transitions::event...>;
    using unique_list = type_list_unique_t<raw_list>;
};

// Helper to convert std::tuple or type_list to type_list
template <typename T>
struct as_type_list;

template <typename... Ts>
struct as_type_list<type_list<Ts...>> {
    using type = type_list<Ts...>;
};

template <typename... Ts>
struct as_type_list<std::tuple<Ts...>> {
    using type = type_list<Ts...>;
};

// Helper to extract deferred events from a single state
template <typename State, typename = void>
struct extract_state_deferred_events {
    using type = type_list<>;
};

template <typename State>
struct extract_state_deferred_events<State, std::void_t<typename State::deferred_events>> {
    using type = typename as_type_list<typename State::deferred_events>::type;
};

// Helper to extract all deferred events from a list of states
template <typename StateList>
struct extract_all_deferred_events;

template <typename... States>
struct extract_all_deferred_events<type_list<States...>> {
    using type = type_list_cat_t<typename extract_state_deferred_events<States>::type...>;
};

// Compile-time detection of exact duplicate (source, event, guard) rows.
template <typename A, typename B>
struct is_duplicate_row {
    static constexpr bool value = std::is_same_v<typename A::source, typename B::source> &&
                                  std::is_same_v<typename A::event, typename B::event> &&
                                  std::is_same_v<typename A::guard_type, typename B::guard_type>;
};

template <typename... Transitions>
struct has_any_duplicate_row;

template <>
struct has_any_duplicate_row<> {
    static constexpr bool value = false;
};

template <typename Head, typename... Tail>
struct has_any_duplicate_row<Head, Tail...> {
    static constexpr bool value = (is_duplicate_row<Head, Tail>::value || ...) || has_any_duplicate_row<Tail...>::value;
};

template <>
struct has_any_duplicate_row<std::tuple<>> {};

template <typename EventList>
struct count_timed_events;

template <typename... Events>
struct count_timed_events<type_list<Events...>> {
    static constexpr std::size_t value = (0 + ... + (is_timed_event_v<Events> ? 1 : 0));
};

template <typename Table, typename = void>
struct table_timed_events_count : std::integral_constant<std::size_t, 0> {};

template <typename Table>
struct table_timed_events_count<Table, std::void_t<typename Table::events>>
    : count_timed_events<typename Table::events> {};

template <typename Table>
inline constexpr std::size_t table_timed_events_count_v = table_timed_events_count<Table>::value;

}  // namespace detail

// Compile-time transition table
template <typename... Transitions>
struct transition_table {
    using transitions_list = type_list<Transitions...>;
    using states = typename detail::extract_states<Transitions...>::unique_list;
    using transition_events = typename detail::extract_transition_events<Transitions...>::unique_list;
    using deferred_events = typename detail::extract_all_deferred_events<states>::type;
    using events = type_list_unique_t<type_list_cat_t<transition_events, deferred_events>>;

    using state_variant = to_variant_t<states>;
    using event_variant = to_variant_t<events>;
    using initial_state = type_list_front_t<states>;

    static constexpr std::size_t state_count = states::size;
    static constexpr std::size_t transition_count = sizeof...(Transitions);
    static constexpr std::size_t event_count = events::size;

    static_assert(!detail::has_any_duplicate_row<Transitions...>::value,
                  "transition_table contains two rows with an identical (source, event, guard) combination; "
                  "the second row is permanently unreachable. Remove the duplicate or differentiate the guard.");

    template <typename S>
    static constexpr bool has_state = type_list_contains_v<S, states>;

    template <typename E>
    static constexpr bool has_event = type_list_contains_v<E, events>;

    std::tuple<Transitions...> rows;

    constexpr transition_table() = default;
    constexpr explicit transition_table(Transitions... trs) : rows(std::move(trs)...) {}
    constexpr explicit transition_table(std::tuple<Transitions...> tr_tuple) : rows(std::move(tr_tuple)) {}
};

// Helper function to create transition_table
template <typename... Transitions>
constexpr auto make_transition_table(Transitions&&... trs) {
    return transition_table<std::decay_t<Transitions>...>(std::forward<Transitions>(trs)...);
}

}  // namespace fsm

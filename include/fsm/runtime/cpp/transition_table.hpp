#pragma once

#include "fsm/runtime/cpp/transition.hpp"
#include "fsm/runtime/cpp/type_traits.hpp"

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
struct extract_events {
    using raw_list = type_list<typename Transitions::event...>;
    using unique_list = type_list_unique_t<raw_list>;
};

}  // namespace detail

// Compile-time transition table
template <typename... Transitions>
struct transition_table {
    using transitions_list = type_list<Transitions...>;
    using states = typename detail::extract_states<Transitions...>::unique_list;
    using events = typename detail::extract_events<Transitions...>::unique_list;

    using state_variant = to_variant_t<states>;
    using event_variant = to_variant_t<events>;
    using initial_state = type_list_front_t<states>;

    static constexpr std::size_t state_count = states::size;
    static constexpr std::size_t transition_count = sizeof...(Transitions);
    static constexpr std::size_t event_count = events::size;

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

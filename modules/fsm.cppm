module;

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module fsm;

export namespace fsm {

// Type list utilities
template <typename... Ts>
struct type_list {};

template <typename Target, typename List>
struct is_in_type_list;

template <typename Target>
struct is_in_type_list<Target, type_list<>> : std::false_type {};

template <typename Target, typename Head, typename... Tail>
struct is_in_type_list<Target, type_list<Head, Tail...>>
    : std::conditional_t<std::is_same_v<Target, Head>, std::true_type, is_in_type_list<Target, type_list<Tail...>>> {};

template <typename Target, typename List>
inline constexpr bool is_in_type_list_v = is_in_type_list<Target, List>::value;

template <typename Target, typename List>
struct type_index;

template <typename Target, typename Head, typename... Tail>
struct type_index<Target, type_list<Head, Tail...>> {
    static constexpr std::size_t value = std::is_same_v<Target, Head> ? 0 : 1 + type_index<Target, type_list<Tail...>>::value;
};

template <typename Target, typename List>
inline constexpr std::size_t type_index_v = type_index<Target, List>::value;

// Null types & defaults
struct no_guard {};
struct no_action {};
struct no_context {};

// Logical Guard Combinators
template <typename Guard>
struct not_ {
    template <typename Event, typename State, typename Context>
    [[nodiscard]] constexpr bool operator()(const Event& evt, const State& st, const Context& ctx) const {
        return !Guard{}(evt, st, ctx);
    }
};

template <typename G1, typename G2>
struct and_ {
    template <typename Event, typename State, typename Context>
    [[nodiscard]] constexpr bool operator()(const Event& evt, const State& st, const Context& ctx) const {
        return G1{}(evt, st, ctx) && G2{}(evt, st, ctx);
    }
};

template <typename G1, typename G2>
struct or_ {
    template <typename Event, typename State, typename Context>
    [[nodiscard]] constexpr bool operator()(const Event& evt, const State& st, const Context& ctx) const {
        return G1{}(evt, st, ctx) || G2{}(evt, st, ctx);
    }
};

// Transition Row Definitions
template <typename Source, typename Event, typename Target, typename Guard = no_guard, typename Action = no_action>
struct row {
    using source_type = Source;
    using event_type = Event;
    using target_type = Target;
    using guard_type = Guard;
    using action_type = Action;
    static constexpr bool is_internal = false;
    static constexpr bool is_choice = false;
};

template <typename Source, typename Event, typename Guard = no_guard, typename Action = no_action>
struct internal_row {
    using source_type = Source;
    using event_type = Event;
    using target_type = Source;
    using guard_type = Guard;
    using action_type = Action;
    static constexpr bool is_internal = true;
    static constexpr bool is_choice = false;
};

template <typename Source, typename Event, typename Target, typename Guard = no_guard, typename Action = no_action>
struct choice_row {
    using source_type = Source;
    using event_type = Event;
    using target_type = Target;
    using guard_type = Guard;
    using action_type = Action;
    static constexpr bool is_internal = false;
    static constexpr bool is_choice = true;
};

template <typename... Rows>
struct transition_table {
    using rows = type_list<Rows...>;
};

// Timed Events
template <uint64_t Milliseconds>
struct after_ms {
    static constexpr uint64_t duration_ms = Milliseconds;
};

template <uint64_t Seconds>
struct after_s {
    static constexpr uint64_t duration_ms = Seconds * 1000;
};

template <uint64_t Microseconds>
struct after_us {
    static constexpr uint64_t duration_us = Microseconds;
};

// Observers
struct null_observer {
    template <typename TransitionInfo>
    void on_transition(const TransitionInfo&) const noexcept {}
    template <typename Event>
    void on_event_dropped(const Event&) const noexcept {}
};

struct console_observer {
    template <typename TransitionInfo>
    void on_transition(const TransitionInfo& info) const {
        std::cout << "[FSM Transition] " << info.source_name << " -> " << info.target_name
                  << " on " << info.event_name << "\n";
    }
    template <typename Event>
    void on_event_dropped(const Event&) const {
        std::cout << "[FSM Dropped Event]\n";
    }
};

}  // namespace fsm

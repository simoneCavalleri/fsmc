#pragma once

#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#if __cplusplus >= 202002L
#include <concepts>
#endif

namespace fsm {

// Default empty context tag
struct no_context {};

#if __cplusplus >= 202002L
template <typename GuardType, typename EventType, typename StateType, typename ContextType>
concept Guard =
    requires(const GuardType& guard, const EventType& event, const StateType& state, const ContextType& context) {
        { guard(event, state, context) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const EventType& event, const StateType& state) {
        { guard(event, state) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const EventType& event) {
        { guard(event) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard) {
        { guard() } -> std::convertible_to<bool>;
    };

template <typename ActionType, typename EventType, typename SrcStateType, typename DstStateType, typename ContextType>
concept Action =
    requires(const ActionType& action, const EventType& event, SrcStateType& src_state, DstStateType& dst_state,
             ContextType& context) { action(event, src_state, dst_state, context); } ||
    requires(const ActionType& action, const EventType& event, SrcStateType& src_state, DstStateType& dst_state) {
        action(event, src_state, dst_state);
    } ||
    requires(const ActionType& action, const EventType& event, DstStateType& dst_state) { action(event, dst_state); } ||
    requires(const ActionType& action, const EventType& event) { action(event); } ||
    requires(const ActionType& action) { action(); };
#endif

// ============================================================================
// Type List Utilities
// ============================================================================

template <typename... Ts>
struct type_list {
    static constexpr std::size_t size = sizeof...(Ts);
};

// Concatenate type_lists
template <typename List1, typename List2>
struct type_list_cat;

template <typename... Ts1, typename... Ts2>
struct type_list_cat<type_list<Ts1...>, type_list<Ts2...>> {
    using type = type_list<Ts1..., Ts2...>;
};

template <typename List1, typename List2>
using type_list_cat_t = typename type_list_cat<List1, List2>::type;

// Check if type_list contains T
template <typename T, typename List>
struct type_list_contains;

template <typename T>
struct type_list_contains<T, type_list<>> : std::false_type {};

template <typename T, typename Head, typename... Tail>
struct type_list_contains<T, type_list<Head, Tail...>>
    : std::conditional_t<std::is_same_v<T, Head>, std::true_type, type_list_contains<T, type_list<Tail...>>> {};

template <typename T, typename List>
inline constexpr bool type_list_contains_v = type_list_contains<T, List>::value;

// Append type to list if not already present
template <typename List, typename T>
struct type_list_append_unique;

template <typename... Ts, typename T>
struct type_list_append_unique<type_list<Ts...>, T> {
    using type = std::conditional_t<type_list_contains_v<T, type_list<Ts...>>, type_list<Ts...>, type_list<Ts..., T>>;
};

// Make unique type_list while preserving first-occurrence order
template <typename List, typename Result = type_list<>>
struct type_list_unique;

template <typename Result>
struct type_list_unique<type_list<>, Result> {
    using type = Result;
};

template <typename Head, typename... Tail, typename Result>
struct type_list_unique<type_list<Head, Tail...>, Result> {
  private:
    using next_result = typename type_list_append_unique<Result, Head>::type;

  public:
    using type = typename type_list_unique<type_list<Tail...>, next_result>::type;
};

template <typename List>
using type_list_unique_t = typename type_list_unique<List>::type;

// Convert type_list to std::variant
template <typename List>
struct to_variant;

template <typename... Ts>
struct to_variant<type_list<Ts...>> {
    using type = std::variant<Ts...>;
};

template <typename List>
using to_variant_t = typename to_variant<List>::type;

// Convert type_list to std::tuple
template <typename List>
struct to_tuple;

template <typename... Ts>
struct to_tuple<type_list<Ts...>> {
    using type = std::tuple<Ts...>;
};

template <typename List>
using to_tuple_t = typename to_tuple<List>::type;

// First element of type_list
template <typename List>
struct type_list_front;

template <typename Head, typename... Tail>
struct type_list_front<type_list<Head, Tail...>> {
    using type = Head;
};

template <typename List>
using type_list_front_t = typename type_list_front<List>::type;

// ============================================================================
// Detection Idiom & Hook Invocations (on_enter, on_exit, guard, action,
// context)
// ============================================================================

namespace detail {

// on_enter(event, ctx)
template <typename State, typename Event, typename Context, typename = void>
struct has_on_enter_event_ctx : std::false_type {};

template <typename State, typename Event, typename Context>
struct has_on_enter_event_ctx<
    State, Event, Context,
    std::void_t<decltype(std::declval<State&>().on_enter(std::declval<const Event&>(), std::declval<Context&>()))>>
    : std::true_type {};

// on_enter(ctx)
template <typename State, typename Context, typename = void>
struct has_on_enter_ctx : std::false_type {};

template <typename State, typename Context>
struct has_on_enter_ctx<State, Context,
                        std::void_t<decltype(std::declval<State&>().on_enter(std::declval<Context&>()))>>
    : std::true_type {};

// on_enter(event)
template <typename State, typename Event, typename = void>
struct has_on_enter_event : std::false_type {};

template <typename State, typename Event>
struct has_on_enter_event<State, Event,
                          std::void_t<decltype(std::declval<State&>().on_enter(std::declval<const Event&>()))>>
    : std::true_type {};

// on_enter()
template <typename State, typename = void>
struct has_on_enter_void : std::false_type {};

template <typename State>
struct has_on_enter_void<State, std::void_t<decltype(std::declval<State&>().on_enter())>> : std::true_type {};

// on_exit(event, ctx)
template <typename State, typename Event, typename Context, typename = void>
struct has_on_exit_event_ctx : std::false_type {};

template <typename State, typename Event, typename Context>
struct has_on_exit_event_ctx<
    State, Event, Context,
    std::void_t<decltype(std::declval<State&>().on_exit(std::declval<const Event&>(), std::declval<Context&>()))>>
    : std::true_type {};

// on_exit(ctx)
template <typename State, typename Context, typename = void>
struct has_on_exit_ctx : std::false_type {};

template <typename State, typename Context>
struct has_on_exit_ctx<State, Context, std::void_t<decltype(std::declval<State&>().on_exit(std::declval<Context&>()))>>
    : std::true_type {};

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
// Safe invocation of on_enter hook (with context and event)
// ----------------------------------------------------------------------------
template <typename State, typename Event, typename Context>
constexpr void call_on_enter(State& state, const Event& event, Context& ctx) {
    if constexpr (detail::has_on_enter_event_ctx<State, Event, Context>::value) {
        state.on_enter(event, ctx);
    } else if constexpr (detail::has_on_enter_ctx<State, Context>::value) {
        state.on_enter(ctx);
    } else if constexpr (detail::has_on_enter_event<State, Event>::value) {
        state.on_enter(event);
    } else if constexpr (detail::has_on_enter_void<State>::value) {
        state.on_enter();
    }
}

// Overload for on_enter without event (initial state enter)
template <typename State, typename Context>
constexpr void call_on_enter(State& state, Context& ctx) {
    if constexpr (detail::has_on_enter_ctx<State, Context>::value) {
        state.on_enter(ctx);
    } else if constexpr (detail::has_on_enter_void<State>::value) {
        state.on_enter();
    }
}

// ----------------------------------------------------------------------------
// Safe invocation of on_exit hook
// ----------------------------------------------------------------------------
template <typename State, typename Event, typename Context>
constexpr void call_on_exit(State& state, const Event& event, Context& ctx) {
    if constexpr (detail::has_on_exit_event_ctx<State, Event, Context>::value) {
        state.on_exit(event, ctx);
    } else if constexpr (detail::has_on_exit_ctx<State, Context>::value) {
        state.on_exit(ctx);
    } else if constexpr (detail::has_on_exit_event<State, Event>::value) {
        state.on_exit(event);
    } else if constexpr (detail::has_on_exit_void<State>::value) {
        state.on_exit();
    }
}

// ----------------------------------------------------------------------------
// Safe invocation of guard
// ----------------------------------------------------------------------------
template <typename Guard, typename Event, typename SrcState, typename Context>
constexpr bool call_guard(const Guard& guard, const Event& event, const SrcState& src, Context& ctx) {
    if constexpr (std::is_invocable_r_v<bool, Guard, const Event&, const SrcState&, Context&>) {
        return guard(event, src, ctx);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Event&, Context&>) {
        return guard(event, ctx);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const SrcState&, Context&>) {
        return guard(src, ctx);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, Context&>) {
        return guard(ctx);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Event&, const SrcState&>) {
        return guard(event, src);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Event&>) {
        return guard(event);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const SrcState&>) {
        return guard(src);
    } else if constexpr (std::is_invocable_r_v<bool, Guard>) {
        return guard();
    } else {
        return true;
    }
}

template <typename Guard, typename Event, typename SrcState, typename Context, typename Fsm>
constexpr bool call_guard(const Guard& guard, const Event& event, const SrcState& src, Context& ctx, const Fsm& fsm) {
    if constexpr (std::is_invocable_r_v<bool, Guard, const Event&, const SrcState&, Context&, const Fsm&>) {
        return guard(event, src, ctx, fsm);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Event&, const SrcState&, const Fsm&>) {
        return guard(event, src, fsm);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Fsm&>) {
        return guard(fsm);
    } else {
        return call_guard(guard, event, src, ctx);
    }
}

// ----------------------------------------------------------------------------
// Safe invocation of action
// ----------------------------------------------------------------------------
template <typename Action, typename Event, typename SrcState, typename DstState, typename Context>
constexpr void call_action(Action& action, const Event& event, SrcState& src, DstState& dst, Context& ctx) {
    if constexpr (std::is_invocable_v<Action, const Event&, SrcState&, DstState&, Context&>) {
        action(event, src, dst, ctx);
    } else if constexpr (std::is_invocable_v<Action, const Event&, SrcState&, Context&>) {
        action(event, src, ctx);
    } else if constexpr (std::is_invocable_v<Action, const Event&, Context&>) {
        action(event, ctx);
    } else if constexpr (std::is_invocable_v<Action, Context&>) {
        action(ctx);
    } else if constexpr (std::is_invocable_v<Action, const Event&, SrcState&, DstState&>) {
        action(event, src, dst);
    } else if constexpr (std::is_invocable_v<Action, const Event&, SrcState&>) {
        action(event, src);
    } else if constexpr (std::is_invocable_v<Action, const Event&>) {
        action(event);
    } else if constexpr (std::is_invocable_v<Action>) {
        action();
    }
}

// ----------------------------------------------------------------------------
// Compile-time Type Name & State Name Reflection
// ----------------------------------------------------------------------------

template <typename T>
constexpr std::string_view get_type_name() {
#if defined(__clang__) || defined(__GNUC__)
    std::string_view name = __PRETTY_FUNCTION__;
    auto start = name.find("T = ");
    if (start != std::string_view::npos) {
        start += 4;
        auto end = name.find_first_of(";]", start);
        if (end != std::string_view::npos) {
            auto full_type = name.substr(start, end - start);
            auto last_scope = full_type.rfind("::");
            if (last_scope != std::string_view::npos) {
                return full_type.substr(last_scope + 2);
            }
            return full_type;
        }
    }
#elif defined(_MSC_VER)
    std::string_view name = __FUNCSIG__;
    auto start = name.find("get_type_name<");
    if (start != std::string_view::npos) {
        start += 14;
        auto end = name.find(">(void)", start);
        if (end == std::string_view::npos) {
            end = name.rfind('>');
        }
        if (end != std::string_view::npos && end > start) {
            auto full_type = name.substr(start, end - start);
            while (full_type.rfind("struct ", 0) == 0) {
                full_type.remove_prefix(7);
            }
            while (full_type.rfind("class ", 0) == 0) {
                full_type.remove_prefix(6);
            }
            while (full_type.rfind("enum ", 0) == 0) {
                full_type.remove_prefix(5);
            }
            auto last_scope = full_type.rfind("::");
            if (last_scope != std::string_view::npos) {
                return full_type.substr(last_scope + 2);
            }
            return full_type;
        }
    }
#endif
    return "UnknownState";
}

namespace detail {

template <typename State, typename = void>
struct has_custom_name_method : std::false_type {};

template <typename State>
struct has_custom_name_method<State, std::void_t<decltype(std::declval<const State&>().name())>> : std::true_type {};

template <typename State, typename = void>
struct has_custom_name_static : std::false_type {};

template <typename State>
struct has_custom_name_static<State, std::void_t<decltype(State::name)>> : std::true_type {};

}  // namespace detail

template <typename State>
constexpr std::string_view get_state_name(const State& state) {
    if constexpr (detail::has_custom_name_method<State>::value) {
        return state.name();
    } else if constexpr (detail::has_custom_name_static<State>::value) {
        return State::name;
    } else {
        return get_type_name<State>();
    }
}

template <typename Event>
constexpr std::string_view get_event_name(const Event& event) {
    if constexpr (detail::has_custom_name_method<Event>::value) {
        return event.name();
    } else if constexpr (detail::has_custom_name_static<Event>::value) {
        return Event::name;
    } else {
        return get_type_name<Event>();
    }
}

// Result of an event dispatch operation
enum class dispatch_status : std::uint8_t {
    success,         // Transition successfully executed
    deferred,        // Event was deferred by the active state
    guard_rejected,  // Transition matched event, but all candidate guard conditions evaluated to false
    unhandled        // No transition defined for (current_state, event)
};

inline constexpr std::string_view to_string(dispatch_status s) noexcept {
    switch (s) {
        case dispatch_status::success:
            return "success";
        case dispatch_status::deferred:
            return "deferred";
        case dispatch_status::guard_rejected:
            return "guard_rejected";
        case dispatch_status::unhandled:
            return "unhandled";
    }
    return "unknown";
}

struct dispatch_result {
    dispatch_status status = dispatch_status::unhandled;

    constexpr dispatch_result() noexcept = default;
    constexpr dispatch_result(dispatch_status s) noexcept : status(s) {}

    [[nodiscard]] constexpr bool is_success() const noexcept { return status == dispatch_status::success; }
    [[nodiscard]] constexpr bool is_deferred() const noexcept { return status == dispatch_status::deferred; }
    [[nodiscard]] constexpr bool is_guard_rejected() const noexcept {
        return status == dispatch_status::guard_rejected;
    }
    [[nodiscard]] constexpr bool is_unhandled() const noexcept { return status == dispatch_status::unhandled; }
    [[nodiscard]] constexpr bool is_ok() const noexcept { return is_success() || is_deferred(); }

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return is_ok(); }
    constexpr bool operator==(const dispatch_result& other) const noexcept { return status == other.status; }
    constexpr bool operator==(dispatch_status other_status) const noexcept { return status == other_status; }
    constexpr bool operator!=(const dispatch_result& other) const noexcept { return status != other.status; }
    constexpr bool operator!=(dispatch_status other_status) const noexcept { return status != other_status; }

    [[nodiscard]] constexpr std::string_view to_string() const noexcept { return fsm::to_string(status); }
};

enum class transition_kind : std::uint8_t { external, internal };

inline constexpr std::string_view to_string(transition_kind k) noexcept {
    switch (k) {
        case transition_kind::external:
            return "external";
        case transition_kind::internal:
            return "internal";
    }
    return "external";
}

// Information about a transition or dispatch attempt passed to observers
struct transition_info {
    std::string_view source;
    std::string_view target;
    std::string_view event;
    dispatch_status status = dispatch_status::success;
    transition_kind kind = transition_kind::external;

    [[nodiscard]] constexpr bool is_internal() const noexcept { return kind == transition_kind::internal; }
    [[nodiscard]] constexpr bool is_external() const noexcept { return kind == transition_kind::external; }
    [[nodiscard]] constexpr bool is_success() const noexcept { return status == dispatch_status::success; }
    [[nodiscard]] constexpr bool is_deferred() const noexcept { return status == dispatch_status::deferred; }
    [[nodiscard]] constexpr bool is_guard_rejected() const noexcept {
        return status == dispatch_status::guard_rejected;
    }
    [[nodiscard]] constexpr bool is_unhandled() const noexcept { return status == dispatch_status::unhandled; }
};

namespace detail {

template <typename T, typename = void>
struct has_name : std::false_type {};

template <typename T>
struct has_name<T, std::void_t<decltype(T::name)>> : std::true_type {};

template <typename State, typename = void>
struct has_parent_name : std::false_type {};

template <typename State>
struct has_parent_name<State, std::void_t<decltype(State::parent)>> : std::true_type {};

template <typename State, typename = void>
struct has_deferred_events : std::false_type {};

template <typename State>
struct has_deferred_events<State, std::void_t<typename State::deferred_events>> : std::true_type {};

template <typename Event, typename List>
struct type_list_contains : std::false_type {};

template <typename Event, typename... Ts>
struct type_list_contains<Event, type_list<Ts...>> : std::disjunction<std::is_same<Event, Ts>...> {};

template <typename Event, typename... Ts>
struct type_list_contains<Event, std::tuple<Ts...>> : std::disjunction<std::is_same<Event, Ts>...> {};

}  // namespace detail

template <typename Event>
constexpr std::string_view event_name() noexcept {
    if constexpr (detail::has_name<std::decay_t<Event>>::value) {
        return std::decay_t<Event>::name;
    } else {
        return "AnonymousEvent";
    }
}

template <typename State>
constexpr std::string_view get_parent_name() noexcept {
    if constexpr (detail::has_parent_name<State>::value) {
        return State::parent;
    } else {
        return "";
    }
}

template <typename State>
inline constexpr bool has_deferred_events_v = detail::has_deferred_events<State>::value;

template <typename State, typename Event>
inline constexpr bool is_deferred_event_v = []() constexpr {
    if constexpr (has_deferred_events_v<State>) {
        return detail::type_list_contains<std::decay_t<Event>, typename State::deferred_events>::value;
    } else {
        return false;
    }
}();

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

// Introspection for History & Deferred events across unique state list
template <typename StateList>
struct any_state_has_history;

template <typename... States>
struct any_state_has_history<type_list<States...>> {
    static constexpr bool value = (detail::has_parent_name<States>::value || ...);
};

template <typename StateList>
struct any_state_has_deferred;

template <typename... States>
struct any_state_has_deferred<type_list<States...>> {
    static constexpr bool value = (detail::has_deferred_events<States>::value || ...);
};

}  // namespace fsm

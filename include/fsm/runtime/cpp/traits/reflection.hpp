#pragma once

#include <string_view>
#include <type_traits>
#include <utility>

namespace fsm {

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

template <typename T, typename = void>
struct has_name : std::false_type {};

template <typename T>
struct has_name<T, std::void_t<decltype(T::name)>> : std::true_type {};

template <typename State, typename = void>
struct has_parent_name : std::false_type {};

template <typename State>
struct has_parent_name<State, std::void_t<decltype(State::parent)>> : std::true_type {};

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

}  // namespace fsm

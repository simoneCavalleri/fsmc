#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <variant>

namespace fsm {

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

}  // namespace fsm

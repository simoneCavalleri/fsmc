#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <variant>

#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(no_unique_address) && __cplusplus >= 202002L
#define FSMC_NO_UNIQUE_ADDRESS [[no_unique_address]]
#elif __has_cpp_attribute(msvc::no_unique_address)
#define FSMC_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define FSMC_NO_UNIQUE_ADDRESS
#endif
#else
#define FSMC_NO_UNIQUE_ADDRESS
#endif

namespace fsm {

// ============================================================================
// Type List Utilities
// ============================================================================

template <typename... Ts>
struct type_list {
    static constexpr std::size_t size = sizeof...(Ts);
};

// Concatenate type_lists (variadic)
template <typename... Lists>
struct type_list_cat;

template <>
struct type_list_cat<> {
    using type = type_list<>;
};

template <typename List>
struct type_list_cat<List> {
    using type = List;
};

template <typename... Ts1, typename... Ts2>
struct type_list_cat<type_list<Ts1...>, type_list<Ts2...>> {
    using type = type_list<Ts1..., Ts2...>;
};

template <typename List1, typename List2, typename... Rest>
struct type_list_cat<List1, List2, Rest...> {
    using type = typename type_list_cat<typename type_list_cat<List1, List2>::type, Rest...>::type;
};

template <typename... Lists>
using type_list_cat_t = typename type_list_cat<Lists...>::type;

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

// Find 0-based index of T in type_list
template <typename T, typename List>
struct type_list_index_of;

template <typename T, typename... Tail>
struct type_list_index_of<T, type_list<T, Tail...>> : std::integral_constant<std::size_t, 0> {};

template <typename T, typename Head, typename... Tail>
struct type_list_index_of<T, type_list<Head, Tail...>> {
  private:
    static constexpr std::size_t tail_val = type_list_index_of<T, type_list<Tail...>>::value;

  public:
    static constexpr std::size_t value =
        (tail_val == static_cast<std::size_t>(-1)) ? static_cast<std::size_t>(-1) : 1 + tail_val;
};

template <typename T>
struct type_list_index_of<T, type_list<>> : std::integral_constant<std::size_t, static_cast<std::size_t>(-1)> {};

template <typename T, typename List>
inline constexpr std::size_t type_list_index_of_v = type_list_index_of<T, List>::value;

}  // namespace fsm

#pragma once

#include <cstddef>
#include <type_traits>

#include "fsm/backend/cpp/runtime/type_traits.hpp"

namespace fsm {

// ============================================================================
// Semantic Policy Modifiers for Policy-Based FSM Configuration
// ============================================================================

template <typename RegistersType>
struct with_registers {
    using type = RegistersType;
};

template <typename InPortsType, typename OutPortsType = no_ports>
struct with_ports {
    using in_type = InPortsType;
    using out_type = OutPortsType;
};

template <typename ServicesType>
struct with_services {
    using type = ServicesType;
};

template <typename ObserverType>
struct with_observer {
    using type = ObserverType;
};

template <typename InitialStateType>
struct with_initial_state {
    using type = InitialStateType;
};

template <std::size_t N>
struct with_deferred_capacity {
    static constexpr std::size_t value = N;
};

template <std::size_t N>
struct with_queue_capacity {
    static constexpr std::size_t value = N;
};

template <std::size_t N>
struct with_timer_capacity {
    static constexpr std::size_t value = N;
};

template <std::size_t N>
struct with_trace_buffer {
    static constexpr std::size_t value = N;
};

// Forward declaration for observer mapping
template <std::size_t Capacity>
class flight_recorder_observer;

// ============================================================================
// Internal Policy Extraction Helpers
// ============================================================================

namespace detail {

// Registers extraction
template <typename Default, typename... Policies>
struct extract_registers {
    using type = Default;
};

template <typename Default, typename R, typename... Rest>
struct extract_registers<Default, with_registers<R>, Rest...> {
    using type = R;
};

template <typename Default, typename Other, typename... Rest>
struct extract_registers<Default, Other, Rest...> : extract_registers<Default, Rest...> {};

// InPorts & OutPorts extraction
template <typename DefaultIn, typename DefaultOut, typename... Policies>
struct extract_ports {
    using in_type = DefaultIn;
    using out_type = DefaultOut;
};

template <typename DefaultIn, typename DefaultOut, typename In, typename Out, typename... Rest>
struct extract_ports<DefaultIn, DefaultOut, with_ports<In, Out>, Rest...> {
    using in_type = In;
    using out_type = Out;
};

template <typename DefaultIn, typename DefaultOut, typename Other, typename... Rest>
struct extract_ports<DefaultIn, DefaultOut, Other, Rest...> : extract_ports<DefaultIn, DefaultOut, Rest...> {};

// Services extraction
template <typename Default, typename... Policies>
struct extract_services {
    using type = Default;
};

template <typename Default, typename Srv, typename... Rest>
struct extract_services<Default, with_services<Srv>, Rest...> {
    using type = Srv;
};

template <typename Default, typename Other, typename... Rest>
struct extract_services<Default, Other, Rest...> : extract_services<Default, Rest...> {};

// Observer extraction
template <typename Default, typename... Policies>
struct extract_observer {
    using type = Default;
};

template <typename Default, typename Obs, typename... Rest>
struct extract_observer<Default, with_observer<Obs>, Rest...> {
    using type = Obs;
};

template <typename Default, std::size_t N, typename... Rest>
struct extract_observer<Default, with_trace_buffer<N>, Rest...> {
    using type = flight_recorder_observer<N>;
};

template <typename Default, typename Other, typename... Rest>
struct extract_observer<Default, Other, Rest...> : extract_observer<Default, Rest...> {};

// Initial State extraction
template <typename Default, typename... Policies>
struct extract_initial_state {
    using type = Default;
};

template <typename Default, typename St, typename... Rest>
struct extract_initial_state<Default, with_initial_state<St>, Rest...> {
    using type = St;
};

template <typename Default, typename Other, typename... Rest>
struct extract_initial_state<Default, Other, Rest...> : extract_initial_state<Default, Rest...> {};

// Deferred Capacity extraction
template <std::size_t Default, typename... Policies>
struct extract_deferred_capacity {
    static constexpr std::size_t value = Default;
};

template <std::size_t Default, std::size_t N, typename... Rest>
struct extract_deferred_capacity<Default, with_deferred_capacity<N>, Rest...> {
    static constexpr std::size_t value = N;
};

template <std::size_t Default, typename Other, typename... Rest>
struct extract_deferred_capacity<Default, Other, Rest...> : extract_deferred_capacity<Default, Rest...> {};

// Queue Capacity extraction
template <std::size_t Default, typename... Policies>
struct extract_queue_capacity {
    static constexpr std::size_t value = Default;
};

template <std::size_t Default, std::size_t N, typename... Rest>
struct extract_queue_capacity<Default, with_queue_capacity<N>, Rest...> {
    static constexpr std::size_t value = N;
};

template <std::size_t Default, typename Other, typename... Rest>
struct extract_queue_capacity<Default, Other, Rest...> : extract_queue_capacity<Default, Rest...> {};

// Timer Capacity extraction
template <std::size_t Default, typename... Policies>
struct extract_timer_capacity {
    static constexpr std::size_t value = Default;
};

template <std::size_t Default, std::size_t N, typename... Rest>
struct extract_timer_capacity<Default, with_timer_capacity<N>, Rest...> {
    static constexpr std::size_t value = N;
};

template <std::size_t Default, typename Other, typename... Rest>
struct extract_timer_capacity<Default, Other, Rest...> : extract_timer_capacity<Default, Rest...> {};

}  // namespace detail

// ============================================================================
// Unified Policy-Based FSM Configuration
// ============================================================================

template <typename Table, typename... Policies>
struct config {
    using table_type = Table;
    using in_ports_type = typename detail::extract_ports<no_ports, no_ports, Policies...>::in_type;
    using out_ports_type = typename detail::extract_ports<no_ports, no_ports, Policies...>::out_type;
    using registers_type = typename detail::extract_registers<no_registers, Policies...>::type;
    using services_type = typename detail::extract_services<no_services, Policies...>::type;
    using initial_state_type = typename detail::extract_initial_state<typename Table::initial_state, Policies...>::type;
    using initial_state = initial_state_type;
    using observer_type = typename detail::extract_observer<no_observer, Policies...>::type;

    static constexpr std::size_t deferred_capacity = detail::extract_deferred_capacity<16, Policies...>::value;
    static constexpr std::size_t queue_capacity = detail::extract_queue_capacity<64, Policies...>::value;
    static constexpr std::size_t timer_capacity = detail::extract_timer_capacity<0, Policies...>::value;
};

// Trait detecting if a type is an fsm::config instantiation
template <typename T>
struct is_config : std::false_type {};

template <typename Table, typename... Policies>
struct is_config<config<Table, Policies...>> : std::true_type {};

template <typename T>
inline constexpr bool is_config_v = is_config<T>::value;

}  // namespace fsm

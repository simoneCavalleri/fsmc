#pragma once

#include "fsm/backend/cpp/runtime/config.hpp"

namespace fsm {

// Partial specialization for policy-based config
template <typename RealTable, typename... Policies, typename InPorts, typename OutPorts, typename Registers,
          typename Services, typename InitialState, typename Observer, std::size_t DeferredCapacity,
          std::size_t TimerCapacity>
class fsm<config<RealTable, Policies...>, InPorts, OutPorts, Registers, Services, InitialState, Observer,
          DeferredCapacity, TimerCapacity>
    : public fsm<RealTable, typename config<RealTable, Policies...>::in_ports_type,
                 typename config<RealTable, Policies...>::out_ports_type,
                 typename config<RealTable, Policies...>::registers_type,
                 typename config<RealTable, Policies...>::services_type,
                 typename config<RealTable, Policies...>::initial_state_type,
                 typename config<RealTable, Policies...>::observer_type,
                 config<RealTable, Policies...>::deferred_capacity,
                 (config<RealTable, Policies...>::timer_capacity > 0
                      ? config<RealTable, Policies...>::timer_capacity
                      : (detail::table_timed_events_count_v<RealTable> > 0
                             ? (detail::table_timed_events_count_v<RealTable> > 4
                                    ? detail::table_timed_events_count_v<RealTable>
                                    : 4)
                             : 0))> {
    static constexpr std::size_t effective_timer_capacity =
        (config<RealTable, Policies...>::timer_capacity > 0
             ? config<RealTable, Policies...>::timer_capacity
             : (detail::table_timed_events_count_v<RealTable> > 0
                    ? (detail::table_timed_events_count_v<RealTable> > 4 ? detail::table_timed_events_count_v<RealTable>
                                                                         : 4)
                    : 0));
    using base_type = fsm<RealTable, typename config<RealTable, Policies...>::in_ports_type,
                          typename config<RealTable, Policies...>::out_ports_type,
                          typename config<RealTable, Policies...>::registers_type,
                          typename config<RealTable, Policies...>::services_type,
                          typename config<RealTable, Policies...>::initial_state_type,
                          typename config<RealTable, Policies...>::observer_type,
                          config<RealTable, Policies...>::deferred_capacity, effective_timer_capacity>;

  public:
    using base_type::base_type;
};

template <typename Table, typename... Policies>
using make_fsm = fsm<config<Table, Policies...>>;

}  // namespace fsm

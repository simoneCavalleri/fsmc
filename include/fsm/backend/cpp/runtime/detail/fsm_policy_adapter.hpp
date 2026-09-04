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
                 config<RealTable, Policies...>::deferred_capacity, TimerCapacity> {
    using base_type = fsm<RealTable, typename config<RealTable, Policies...>::in_ports_type,
                          typename config<RealTable, Policies...>::out_ports_type,
                          typename config<RealTable, Policies...>::registers_type,
                          typename config<RealTable, Policies...>::services_type,
                          typename config<RealTable, Policies...>::initial_state_type,
                          typename config<RealTable, Policies...>::observer_type,
                          config<RealTable, Policies...>::deferred_capacity, TimerCapacity>;

  public:
    using base_type::base_type;
};

template <typename Table, typename... Policies>
using make_fsm = fsm<config<Table, Policies...>>;

}  // namespace fsm

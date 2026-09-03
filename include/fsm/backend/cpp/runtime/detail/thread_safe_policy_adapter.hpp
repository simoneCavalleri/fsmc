#pragma once

#include "fsm/backend/cpp/runtime/config.hpp"

namespace fsm {

// Partial specialization for policy-based config
template <typename RealTable, typename... Policies, typename InPorts, typename OutPorts, typename Registers,
          typename Services, typename InitialState, std::size_t DeferredCapacity>
class thread_safe_fsm<config<RealTable, Policies...>, InPorts, OutPorts, Registers, Services, InitialState,
                      DeferredCapacity>
    : public thread_safe_fsm<RealTable, typename config<RealTable, Policies...>::in_ports_type,
                             typename config<RealTable, Policies...>::out_ports_type,
                             typename config<RealTable, Policies...>::registers_type,
                             typename config<RealTable, Policies...>::services_type,
                             typename config<RealTable, Policies...>::initial_state_type,
                             config<RealTable, Policies...>::deferred_capacity> {
    using base_type = thread_safe_fsm<
        RealTable, typename config<RealTable, Policies...>::in_ports_type,
        typename config<RealTable, Policies...>::out_ports_type,
        typename config<RealTable, Policies...>::registers_type, typename config<RealTable, Policies...>::services_type,
        typename config<RealTable, Policies...>::initial_state_type, config<RealTable, Policies...>::deferred_capacity>;

  public:
    using base_type::base_type;
};

template <typename Table, typename... Policies>
using make_thread_safe_fsm = thread_safe_fsm<config<Table, Policies...>>;

}  // namespace fsm

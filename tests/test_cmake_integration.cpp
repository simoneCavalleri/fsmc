#include "connection_fsm.hpp"

#include <cassert>
#include <iostream>

int main() {
    std::cout << "=================================================\n"
              << "  RUNNING CMAKE INTEGRATION (fsm_target_sources) \n"
              << "=================================================\n";

    net_cmake::ConnectionFSM sm;

    assert(sm.current_state_name() == "Disconnected");
    assert(sm.is_in_state<net_cmake::Disconnected>());

    sm.dispatch(net_cmake::ConnectCmd{});
    assert(sm.current_state_name() == "Connecting");
    assert(sm.is_in_state<net_cmake::Connecting>());

    sm.dispatch(net_cmake::HandshakeOkEvent{});
    assert(sm.current_state_name() == "Connected");
    assert(sm.is_in_state<net_cmake::Connected>());

    std::cout << "[PASS] fsm_target_sources CMake integration test passed successfully!\n"
              << "=================================================\n";
    return 0;
}

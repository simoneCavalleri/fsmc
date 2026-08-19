#include <cassert>
#include <iostream>

#include "connection_fsm.hpp"

int main() {
    std::cout << "=================================================\n"
              << "  RUNNING C++20 STANDALONE GENERATED FSM TEST    \n"
              << "=================================================\n";

    net20::ConnectionFSM20 state_machine;

    assert(state_machine.current_state_name() == "Disconnected");
    assert(state_machine.is_in_state<net20::Disconnected>());

    state_machine.dispatch(net20::ConnectCmd{});
    assert(state_machine.current_state_name() == "Connecting");
    assert(state_machine.is_in_state<net20::Connecting>());

    state_machine.dispatch(net20::HandshakeOkEvent{});
    assert(state_machine.current_state_name() == "Connected");
    assert(state_machine.is_in_state<net20::Connected>());

    state_machine.dispatch(net20::NetworkDegradedEvent{});
    assert(state_machine.current_state_name() == "Suspended");
    assert(state_machine.is_in_state<net20::Suspended>());

    state_machine.dispatch(net20::NetworkRestoredEvent{});
    assert(state_machine.current_state_name() == "Connected");
    assert(state_machine.is_in_state<net20::Connected>());

    state_machine.dispatch(net20::DisconnectCmd{});
    assert(state_machine.current_state_name() == "Disconnected");
    assert(state_machine.is_in_state<net20::Disconnected>());

    // Test C++20 ThreadSafe with std::jthread
    net20::ThreadSafeConnectionFSM20 async_machine;
    async_machine.start_worker();
    async_machine.post(net20::ConnectCmd{});
    while (!async_machine.is_in_state<net20::Connecting>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    async_machine.stop_worker();

    std::cout << "[PASS] C++20 Standalone FSM (Concepts + std::jthread) test passed successfully!\n"
              << "=================================================\n";
    return 0;
}

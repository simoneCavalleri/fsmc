#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

#include "../examples/connection_manager/connection_fsm.hpp"

namespace net {
struct NetworkContext {
    int socket_fd = -1;
    bool is_online = true;
};
}  // namespace net

int main() {
    std::cout << "=================================================\n"
              << "  RUNNING COMPILED GENERATED FSM TEST SUITE     \n"
              << "=================================================\n";

    net::NetworkContext context;
    net::ConnectionFSM state_machine(context);

    // Initial state: Disconnected
    assert(state_machine.current_state_name() == "Disconnected");
    assert(state_machine.is_in_state<net::Disconnected>());

    // Dispatch ConnectCmd -> should transition to Connecting
    state_machine.dispatch(net::ConnectCmd{});
    assert(state_machine.current_state_name() == "Connecting");
    assert(state_machine.is_in_state<net::Connecting>());

    // Dispatch HandshakeOkEvent -> should transition to Connected
    state_machine.dispatch(net::HandshakeOkEvent{});
    assert(state_machine.current_state_name() == "Connected");
    assert(state_machine.is_in_state<net::Connected>());

    // Dispatch NetworkDegradedEvent -> should transition to Suspended
    state_machine.dispatch(net::NetworkDegradedEvent{});
    assert(state_machine.current_state_name() == "Suspended");
    assert(state_machine.is_in_state<net::Suspended>());

    // Dispatch NetworkRestoredEvent -> should transition back to Connected
    state_machine.dispatch(net::NetworkRestoredEvent{});
    assert(state_machine.current_state_name() == "Connected");
    assert(state_machine.is_in_state<net::Connected>());

    // Dispatch DisconnectCmd -> should transition to Disconnected
    state_machine.dispatch(net::DisconnectCmd{});
    assert(state_machine.current_state_name() == "Disconnected");
    assert(state_machine.is_in_state<net::Disconnected>());

    // Test ThreadSafe wrapper
    net::ThreadSafeConnectionFSM async_machine(context);
    async_machine.start_worker();
    async_machine.post(net::ConnectCmd{});
    while (!async_machine.is_in_state<net::Connecting>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    async_machine.stop_worker();

    std::cout << "[PASS] Generated C++ FSM (Connection Manager) compiles and executes transitions successfully!\n"
              << "=================================================\n";
    return 0;
}

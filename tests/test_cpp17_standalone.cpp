#include <cassert>
#include <iostream>

#include "connection_fsm.hpp"

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << "  RUNNING C++17 STANDALONE GENERATED FSM TEST    " << std::endl;
    std::cout << "=================================================" << std::endl;

    net17::ConnectionFSM17 sm;

    assert(sm.current_state_name() == "Disconnected");
    assert(sm.is_in_state<net17::Disconnected>());

    sm.dispatch(net17::ConnectCmd{});
    assert(sm.current_state_name() == "Connecting");
    assert(sm.is_in_state<net17::Connecting>());

    sm.dispatch(net17::HandshakeOkEvent{});
    assert(sm.current_state_name() == "Connected");
    assert(sm.is_in_state<net17::Connected>());

    sm.dispatch(net17::NetworkDegradedEvent{});
    assert(sm.current_state_name() == "Suspended");
    assert(sm.is_in_state<net17::Suspended>());

    sm.dispatch(net17::NetworkRestoredEvent{});
    assert(sm.current_state_name() == "Connected");
    assert(sm.is_in_state<net17::Connected>());

    sm.dispatch(net17::DisconnectCmd{});
    assert(sm.current_state_name() == "Disconnected");
    assert(sm.is_in_state<net17::Disconnected>());

    // Test ThreadSafe
    net17::ThreadSafeConnectionFSM17 async_sm;
    async_sm.start_worker();
    async_sm.post(net17::ConnectCmd{});
    while (!async_sm.is_in_state<net17::Connecting>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    async_sm.stop_worker();

    std::cout << "[PASS] C++17 Standalone FSM test passed successfully!" << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}

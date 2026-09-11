/**
 * @file test_generated_fsm.cpp
 * @brief Standalone executable validating compilation and runtime dispatch of generated FSM code.
 * @scenario Instantiate generated ConnectionMermaidFSM state machine and dispatch synchronous and asynchronous
 * transitions.
 * @expected State machine initializes in Disconnected state, performs synchronous state transitions, and processes
 * asynchronous transitions cleanly.
 */

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

#include "connection_fsm.hpp"

int main() {
    std::cout << "=================================================\n"
              << "  RUNNING COMPILED GENERATED FSM TEST SUITE     \n"
              << "=================================================\n";

    net_mermaid::ConnectionMermaidFSM state_machine;

    // Initial state: Disconnected
    assert(state_machine.current_state_name() == "Disconnected");
    assert(state_machine.is_in_state<net_mermaid::Disconnected>());

    // Dispatch ConnectCmd -> should transition to Connecting
    state_machine.dispatch(net_mermaid::ConnectCmd{});
    assert(state_machine.current_state_name() == "Connecting");
    assert(state_machine.is_in_state<net_mermaid::Connecting>());

    // Dispatch HandshakeOkEvent -> should transition to Connected
    state_machine.dispatch(net_mermaid::HandshakeOkEvent{});
    assert(state_machine.current_state_name() == "Connected");
    assert(state_machine.is_in_state<net_mermaid::Connected>());

    // Dispatch NetworkDegradedEvent -> should transition to Suspended
    state_machine.dispatch(net_mermaid::NetworkDegradedEvent{});
    assert(state_machine.current_state_name() == "Suspended");
    assert(state_machine.is_in_state<net_mermaid::Suspended>());

    // Dispatch NetworkRestoredEvent -> should transition back to Connected
    state_machine.dispatch(net_mermaid::NetworkRestoredEvent{});
    assert(state_machine.current_state_name() == "Connected");
    assert(state_machine.is_in_state<net_mermaid::Connected>());

    // Dispatch DisconnectCmd -> should transition to Disconnected
    state_machine.dispatch(net_mermaid::DisconnectCmd{});
    assert(state_machine.current_state_name() == "Disconnected");
    assert(state_machine.is_in_state<net_mermaid::Disconnected>());

    // Test ThreadSafe wrapper
    net_mermaid::ThreadSafeConnectionMermaidFSM async_machine;
    async_machine.start_worker();
    async_machine.post(net_mermaid::ConnectCmd{});
    while (!async_machine.is_in_state<net_mermaid::Connecting>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    async_machine.stop_worker();

    std::cout
        << "[PASS] Generated C++ FSM (Connection Manager Mermaid) compiles and executes transitions successfully!\n"
        << "=================================================\n";
    return 0;
}

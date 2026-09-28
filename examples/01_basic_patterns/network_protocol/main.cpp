/**
 * @file main.cpp
 * @brief Executable verification test harness for High-Throughput Network Protocol Handshake.
 */

#include <cassert>

// VERIFY_EXPR: like assert(), but evaluates the expression in all build
// configurations (including NDEBUG / release) so the result variable is never
// considered unused by the compiler.
#ifndef VERIFY_EXPR
#define VERIFY_EXPR(expr) \
    do {                  \
        if (!(expr)) {    \
            std::abort(); \
        }                 \
    } while (false)
#endif
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <thread>

#include "connection_fsm.hpp"

namespace net {

struct NetworkInPorts {
    bool has_network_interface = true;
    bool has_valid_credentials = true;
};

struct NetworkRegisters {
    int socket_fd = -1;
    int session_id = 0;
    bool queue_paused = false;
    int error_count = 0;
    uint64_t bytes_transferred = 0;
};

// ============================================================================
// Custom Atomic Guard Functors
// ============================================================================
struct HasNetworkGuard {
    [[nodiscard]] constexpr bool operator()(const NetworkInPorts& in) const noexcept {
        return in.has_network_interface;
    }
};

struct HasValidCredentialsGuard {
    [[nodiscard]] constexpr bool operator()(const NetworkInPorts& in) const noexcept {
        return in.has_valid_credentials;
    }
};

// ============================================================================
// Custom Action Functors
// ============================================================================
struct InitSocketAction {
    void operator()(NetworkRegisters& reg) const {
        reg.socket_fd = 42;
        std::cout << "\033[1;32m  [NET Action]\033[0m Physical link UP -> Non-blocking TCP socket opened (fd="
                  << reg.socket_fd << ")\n";
    }
};

struct LogErrorAction {
    void operator()(NetworkRegisters& reg) const {
        reg.error_count++;
        std::cout << "\033[1;31m  [NET Action/ERROR]\033[0m Network interface or credentials MISSING -> Connection "
                     "rejected (Total errors: "
                  << reg.error_count << ")\n";
    }
};

struct SetupSessionAction {
    void operator()(NetworkRegisters& reg) const {
        reg.session_id = 999;
        std::cout
            << "\033[1;32m  [NET Action]\033[0m Handshake ACK received -> Cryptographic session active (session_id="
            << reg.session_id << ")\n";
    }
};

struct PauseQueueAction {
    void operator()(NetworkRegisters& reg) const {
        reg.queue_paused = true;
        std::cout << "\033[1;33m  [NET Action]\033[0m Network degraded -> Outgoing TCP transmission buffer paused\n";
    }
};

struct ResumeQueueAction {
    void operator()(NetworkRegisters& reg) const {
        reg.queue_paused = false;
        std::cout << "\033[1;32m  [NET Action]\033[0m Network restored -> Transmission buffer resumed\n";
    }
};

struct CloseSocketAction {
    void operator()(NetworkRegisters& reg) const {
        std::cout << "\033[1;34m  [NET Action]\033[0m Graceful disconnect -> TCP FIN packet sent; socket "
                  << reg.socket_fd << " closed\n";
        reg.socket_fd = -1;
        reg.session_id = 0;
    }
};

struct CleanupAction {
    void operator()(NetworkRegisters& reg) const {
        std::cout
            << "\033[1;31m  [NET Action]\033[0m Sudden link teardown/timeout -> Resetting socket & flushing buffers\n";
        reg.socket_fd = -1;
        reg.session_id = 0;
        reg.queue_paused = false;
    }
};

}  // namespace net

// ============================================================================
// Visual Pretty-Printing Helpers
// ============================================================================
static void print_section(std::string_view title) {
    std::cout << "\n\033[1;36m================================================================================\033[0m\n"
              << "\033[1;37m " << title << "\033[0m\n"
              << "\033[1;36m================================================================================\033[0m\n";
}

static void print_step(std::string_view step, std::string_view state) {
    std::cout << "  \033[1;35m[STEP]\033[0m " << std::left << std::setw(42) << step << " --> Current State: \033[1;32m"
              << state << "\033[0m\n";
}

// ============================================================================
// Verification Suite Execution
// ============================================================================
int main() {
    print_section("FSMC SHOWCASE 01: HIGH-PERFORMANCE RESILIENT NETWORK PROTOCOL");

    net::NetworkInPorts in_ports;
    fsm::no_ports out_ports;

    // Modern Policy-Based Configuration:
    // - Typed InPorts
    // - Discrete Datapath Registers
    // - Blackbox Circular Flight Recorder with 16-element Ring Buffer
    using ResilientNetworkFSM = fsm::make_fsm<net::ConnectionFSMTable, fsm::with_ports<net::NetworkInPorts>,
                                              fsm::with_registers<net::NetworkRegisters>, fsm::with_trace_buffer<16>>;

    ResilientNetworkFSM sm(net::NetworkRegisters{});

    // Initial state: Disconnected
    assert(sm.current_state_name() == "Disconnected");
    assert(sm.is_in<net::Disconnected>());
    assert(sm.is_invariant_satisfied());
    print_step("Initial State Machine Instantiation", sm.current_state_name());

    // ------------------------------------------------------------------------
    // Scenario 1: Guard Failure Check (De Morgan Negative Path)
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Scenario 1: Negative Path (Missing Network Interface) ---\033[0m\n";
    in_ports.has_network_interface = false;
    auto res1 = sm.dispatch(net::ConnectCmd{}, in_ports, out_ports);
    assert(res1.is_success());  // Self-transition via LogErrorAction executed
    assert(sm.is_in<net::Disconnected>());
    assert(sm.registers().error_count == 1);
    assert(sm.registers().socket_fd == -1);
    (void)res1;
    print_step("ConnectCmd with has_network=false", sm.current_state_name());

    // ------------------------------------------------------------------------
    // Scenario 2: Successful Connection Lifecycle & Invariants Check
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Scenario 2: Nominal Connection Lifecycle ---\033[0m\n";
    in_ports.has_network_interface = true;
    in_ports.has_valid_credentials = true;

    auto res2 = sm.dispatch(net::ConnectCmd{}, in_ports, out_ports);
    assert(res2.is_success());
    assert(sm.is_in<net::Connecting>());
    assert(sm.registers().socket_fd == 42);
    assert(sm.is_invariant_satisfied());
    (void)res2;
    print_step("ConnectCmd with valid credentials", sm.current_state_name());

    // Advance clock deterministically by 25ms during handshake
    sm.tick(std::chrono::milliseconds(25));
    assert(sm.is_invariant_satisfied());

    auto res3 = sm.dispatch(net::HandshakeOkEvent{}, in_ports, out_ports);
    assert(res3.is_success());
    assert(sm.is_in<net::Connected>());
    assert(sm.registers().session_id == 999);
    (void)res3;
    print_step("HandshakeOkEvent received", sm.current_state_name());

    // ------------------------------------------------------------------------
    // Scenario 3: Network Degradation and Recovery
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Scenario 3: Transient Network Degradation & Resumption ---\033[0m\n";
    auto res4 = sm.dispatch(net::NetworkDegradedEvent{}, in_ports, out_ports);
    assert(res4.is_success());
    assert(sm.is_in<net::Suspended>());
    assert(sm.registers().queue_paused == true);
    (void)res4;
    print_step("NetworkDegradedEvent received", sm.current_state_name());

    sm.tick(std::chrono::milliseconds(50));

    auto res5 = sm.dispatch(net::NetworkRestoredEvent{}, in_ports, out_ports);
    assert(res5.is_success());
    assert(sm.is_in<net::Connected>());
    assert(sm.registers().queue_paused == false);
    (void)res5;
    print_step("NetworkRestoredEvent received", sm.current_state_name());

    // ------------------------------------------------------------------------
    // Scenario 4: Clean Session Teardown & Flight Recorder Audit Dump
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Scenario 4: Clean Session Teardown ---\033[0m\n";
    auto res6 = sm.dispatch(net::DisconnectCmd{}, in_ports, out_ports);
    assert(res6.is_success());
    assert(sm.is_in<net::Disconnected>());
    assert(sm.registers().socket_fd == -1);
    (void)res6;
    print_step("DisconnectCmd issued", sm.current_state_name());

    std::cout << "\n\033[1;36m--- Circular Flight Recorder Audit Trace (with_trace_buffer<16>) ---\033[0m\n";
    sm.observer().recorder().dump(std::cout);

    // ------------------------------------------------------------------------
    // Scenario 5: Asynchronous Worker Execution (thread_safe_fsm)
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Scenario 5: Thread-Safe Concurrency & Register Snapshots ---\033[0m\n";
    using MyAsyncFSM =
        fsm::make_thread_safe_fsm<net::ConnectionFSMTable, fsm::with_ports<net::NetworkInPorts>,
                                  fsm::with_registers<net::NetworkRegisters>, fsm::with_queue_capacity<64>>;
    MyAsyncFSM async_sm(net::NetworkRegisters{});

    async_sm.start_worker();

    auto fut1 = async_sm.post_async(net::ConnectCmd{});
    auto status1 = fut1.get();
    VERIFY_EXPR(status1.is_success());
    assert(async_sm.current_state_name() == "Connecting");
    print_step("Asynchronous ConnectCmd processed", async_sm.current_state_name());

    auto fut2 = async_sm.post_async(net::HandshakeOkEvent{});
    auto status2 = fut2.get();
    VERIFY_EXPR(status2.is_success());
    assert(async_sm.current_state_name() == "Connected");
    print_step("Asynchronous HandshakeOkEvent processed", async_sm.current_state_name());

    // Safe register modification & lock-free snapshot inspection
    async_sm.with_registers([](auto& reg) { reg.bytes_transferred += 65536; });

    auto snap = async_sm.snapshot_registers();
    assert(snap.session_id == 999);
    assert(snap.bytes_transferred == 65536);
    std::cout << "  \033[1;32m[THREAD-SAFE SNAPSHOT]\033[0m Verified: Session ID=" << snap.session_id
              << ", Bytes Transferred=" << snap.bytes_transferred << "\n";

    async_sm.stop_worker();

    print_section("ALL RESILIENT NETWORK PROTOCOL TESTS PASSED [100% OK]");
    return 0;
}

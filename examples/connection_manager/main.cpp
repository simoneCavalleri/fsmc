#include <cassert>
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
        reg.session_id = 1001;
        reg.bytes_transferred += 128;
        std::cout << "\033[1;32m  [NET Action]\033[0m TLS 1.3 Handshake COMPLETE -> Authenticated Session #"
                  << reg.session_id << " established.\n";
    }
};

struct CleanupAction {
    void operator()(NetworkRegisters& reg) const {
        reg.socket_fd = -1;
        reg.session_id = 0;
        reg.queue_paused = false;
        std::cout << "\033[1;33m  [NET Action]\033[0m Connection terminated -> Socket buffers flushed & resources "
                     "reclaimed.\n";
    }
};

struct PauseQueueAction {
    void operator()(NetworkRegisters& reg) const {
        reg.queue_paused = true;
        std::cout << "\033[1;33m  [NET Action]\033[0m High packet loss detected (>15%) -> Outbound packet queue "
                     "PAUSED.\n";
    }
};

struct ResumeQueueAction {
    void operator()(NetworkRegisters& reg) const {
        reg.queue_paused = false;
        std::cout << "\033[1;32m  [NET Action]\033[0m Link latency stabilized (<20ms) -> Outbound packet queue "
                     "RESUMED.\n";
    }
};

struct CloseSocketAction {
    void operator()(NetworkRegisters& reg) const {
        reg.socket_fd = -1;
        reg.session_id = 0;
        std::cout << "\033[1;32m  [NET Action]\033[0m FIN-ACK sequence complete -> Socket closed gracefully.\n";
    }
};

using AppConnectionFSM = ::fsm::fsm<ConnectionFSMTable, NetworkInPorts, ::fsm::no_ports, NetworkRegisters>;
using AppThreadSafeConnectionFSM = ::fsm::thread_safe_fsm<ConnectionFSMTable, NetworkInPorts, ::fsm::no_ports, NetworkRegisters>;

}  // namespace net

namespace {

void print_header(std::string_view title) {
    std::cout << "\n\033[1;35m======================================================================\033[0m\n";
    std::cout << "\033[1;37m  " << title << "\033[0m\n";
    std::cout << "\033[1;35m======================================================================\033[0m\n";
}

void print_network_hud(const net::NetworkRegisters& reg, std::string_view state_name) {
    std::cout << "  ┌─────────────────────────────────────────────────────────────┐\n";
    std::cout << "  │ \033[1mState:\033[0m " << std::left << std::setw(15) << state_name << " │ \033[1mSocket:\033[0m  "
              << std::left << std::setw(6) << (reg.socket_fd >= 0 ? ("fd=" + std::to_string(reg.socket_fd)) : "CLOSED")
              << " │ \033[1mSession:\033[0m " << (reg.session_id > 0 ? ("#" + std::to_string(reg.session_id)) : "NONE ")
              << "      │\n";
    std::cout << "  │ \033[1mQueue:\033[0m " << std::left << std::setw(15)
              << (reg.queue_paused ? "\033[33mPAUSED\033[0m" : "\033[32mACTIVE\033[0m") << " │ \033[1mErrors:\033[0m  "
              << std::left << std::setw(6) << reg.error_count << " │ \033[1mTransferred:\033[0m "
              << reg.bytes_transferred << " B    │\n";
    std::cout << "  └─────────────────────────────────────────────────────────────┘\n";
}

}  // namespace

int main() {
    print_header("fsmc Networking Showcase: Resilient Connection Protocol (SysML / C++20)");

    net::NetworkInPorts in;
    net::NetworkRegisters reg;
    ::fsm::no_ports out;
    in.has_network_interface = true;
    in.has_valid_credentials = true;

    // 1. Instantiate the State Machine with NetworkRegisters
    net::AppConnectionFSM fsm(reg);

    // Attach live telemetry observer
    fsm.set_observer([](const fsm::transition_info& info) {
        std::cout << "\033[1;34m[NET OBSERVER]\033[0m " << info.source << " --(\033[1m" << info.event << "\033[0m)--> "
                  << info.target << (info.is_internal() ? " \033[36m[INTERNAL]\033[0m" : "") << "\n";
    });

    // Phase 1: Initial State Verification
    print_header("PHASE 1: Initial State & Hardware Network Interface Verification");
    print_network_hud(fsm.registers(), fsm.current_state_name());
    assert(fsm.is_in_state<net::Disconnected>());

    // Phase 2: Establishing Connection (Guarded Composite Transition)
    print_header("PHASE 2: TLS Connection Handshake (Guarded Boolean Transition)");
    std::cout << "\n--> [App] Dispatching ConnectCmd (HasNetwork && HasValidCredentials)...\n";
    auto res = fsm.dispatch(net::ConnectCmd{}, in, out);
    assert(res.is_success());
    assert(fsm.is_in_state<net::Connecting>());
    print_network_hud(fsm.registers(), fsm.current_state_name());

    std::cout << "\n--> [App] Receiving TLS Handshake Certificate validation...\n";
    res = fsm.dispatch(net::HandshakeOkEvent{}, in, out);
    assert(res.is_success());
    assert(fsm.is_in_state<net::Connected>());
    assert(fsm.registers().session_id == 1001);
    print_network_hud(fsm.registers(), fsm.current_state_name());

    // Phase 3: Network Degradation & Automatic Flow Control
    print_header("PHASE 3: QoS Degradation & Automatic Packet Queue Flow Control");
    std::cout << "\n--> [App] Signal degradation detected (Packet loss alert)...\n";
    res = fsm.dispatch(net::NetworkDegradedEvent{}, in, out);
    assert(res.is_success());
    assert(fsm.is_in_state<net::Suspended>());
    assert(fsm.registers().queue_paused);
    print_network_hud(fsm.registers(), fsm.current_state_name());

    std::cout << "\n--> [App] Network quality recovered -> Resuming outbound flow...\n";
    res = fsm.dispatch(net::NetworkRestoredEvent{}, in, out);
    assert(res.is_success());
    assert(fsm.is_in_state<net::Connected>());
    assert(!fsm.registers().queue_paused);
    print_network_hud(fsm.registers(), fsm.current_state_name());

    // Phase 4: Graceful Disconnection
    print_header("PHASE 4: Graceful Session Termination");
    std::cout << "\n--> [App] Dispatching DisconnectCmd...\n";
    res = fsm.dispatch(net::DisconnectCmd{}, in, out);
    assert(res.is_success());
    assert(fsm.is_in_state<net::Disconnected>());
    print_network_hud(fsm.registers(), fsm.current_state_name());

    // Phase 5: De Morgan Boolean Guard Rejection Tests
    print_header("PHASE 5: De Morgan Composite Boolean Guard Enforcement");
    std::cout << "\n  \033[33m[Test 5A]\033[0m Attempting connection without physical network interface:\n";
    {
        net::NetworkInPorts offline_in;
        offline_in.has_network_interface = false;
        net::NetworkRegisters offline_reg;
        net::AppConnectionFSM offline_fsm(offline_reg);
        offline_fsm.dispatch(net::ConnectCmd{}, offline_in, out);
        assert(offline_fsm.is_in_state<net::Disconnected>());
        assert(offline_fsm.registers().error_count == 1);
        std::cout << "  (Connection safely rejected by guard [!HasNetwork || !HasValidCredentials]! State remains: "
                  << offline_fsm.current_state_name() << ")\n";
    }

    std::cout << "\n  \033[33m[Test 5B]\033[0m Attempting connection with invalid cryptographic credentials:\n";
    {
        net::NetworkInPorts invalid_cred_in;
        invalid_cred_in.has_valid_credentials = false;
        net::NetworkRegisters cred_reg;
        net::AppConnectionFSM cred_fsm(cred_reg);
        cred_fsm.dispatch(net::ConnectCmd{}, invalid_cred_in, out);
        assert(cred_fsm.is_in_state<net::Disconnected>());
        assert(cred_fsm.registers().error_count == 1);
        std::cout << "  (Connection safely rejected by credentials validation guard! State remains: "
                  << cred_fsm.current_state_name() << ")\n";
    }

    // Phase 6: Handshake Timeout Recovery
    print_header("PHASE 6: TCP / TLS Handshake Timeout Recovery");
    {
        net::NetworkRegisters timeout_reg;
        net::AppConnectionFSM timeout_fsm(timeout_reg);
        timeout_fsm.dispatch(net::ConnectCmd{}, in, out);
        std::cout << "  State: " << timeout_fsm.current_state_name() << " -> Simulating 5000ms handshake timeout...\n";
        timeout_fsm.dispatch(net::TimeoutEvent{}, in, out);
        assert(timeout_fsm.is_in_state<net::Disconnected>());
        assert(timeout_fsm.registers().socket_fd == -1);
        std::cout << "  (Timeout safely triggered cleanup and transitioned back to: "
                  << timeout_fsm.current_state_name() << ")\n";
    }

    // Phase 7: Multithreaded Asynchronous Worker Execution
    print_header("PHASE 7: Asynchronous Background Thread-Safe Worker Execution");
    net::NetworkRegisters async_reg;
    net::AppThreadSafeConnectionFSM async_fsm(async_reg);
    async_fsm.start_worker();

    std::cout << "  Posting events asynchronously to background thread...\n";
    async_fsm.post(net::ConnectCmd{});
    async_fsm.post(net::HandshakeOkEvent{});

    while (!async_fsm.is_in_state<net::Connected>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cout << "  \033[32m[SUCCESS]\033[0m Async connection FSM transitioned to Connected under worker thread!\n";
    async_fsm.stop_worker();

    std::cout << "\n\033[1;32m======================================================================\033[0m\n";
    std::cout << "\033[1;32m  [SUCCESS] All Connection Manager features verified successfully!    \033[0m\n";
    std::cout << "\033[1;32m======================================================================\033[0m\n";
    return 0;
}

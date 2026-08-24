#include <cassert>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <thread>

// Forward declaration of custom context in namespace net
namespace net {
struct NetworkContext {
    bool has_network_interface = true;
    bool has_valid_credentials = true;
    int socket_fd = -1;
    int session_id = 0;
    bool queue_paused = false;
    int error_count = 0;
    uint64_t bytes_transferred = 0;
};
}  // namespace net

#include "connection_fsm.hpp"

namespace net {

// ============================================================================
// Custom Atomic Guard Functors
// Combined automatically via C++ templates:
//   Nominal path: fsm::and_<HasNetworkGuard, HasValidCredentialsGuard>
//   Error path (De Morgan): fsm::or_<fsm::not_<HasNetworkGuard>, fsm::not_<HasValidCredentialsGuard>>
// ============================================================================
struct HasNetworkGuard {
    [[nodiscard]] constexpr bool operator()(const ConnectCmd& /*evt*/, const auto& /*src*/,
                                            const NetworkContext& ctx) const noexcept {
        return ctx.has_network_interface;
    }
};

struct HasValidCredentialsGuard {
    [[nodiscard]] constexpr bool operator()(const ConnectCmd& /*evt*/, const auto& /*src*/,
                                            const NetworkContext& ctx) const noexcept {
        return ctx.has_valid_credentials;
    }
};

// ============================================================================
// Custom Action Functors
// ============================================================================
struct InitSocketAction {
    constexpr void operator()(const ConnectCmd& /*evt*/, auto& /*src*/, auto& /*dst*/, NetworkContext& ctx) const {
        ctx.socket_fd = 42;
        std::cout << "\033[1;32m  [NET Action]\033[0m Physical link UP -> Non-blocking TCP socket opened (fd="
                  << ctx.socket_fd << ")\n";
    }
};

struct LogErrorAction {
    constexpr void operator()(const ConnectCmd& /*evt*/, auto& /*src*/, auto& /*dst*/, NetworkContext& ctx) const {
        ctx.error_count++;
        std::cout << "\033[1;31m  [NET Action/ERROR]\033[0m Network interface or credentials MISSING -> Connection "
                     "rejected (Total errors: "
                  << ctx.error_count << ")\n";
    }
};

struct SetupSessionAction {
    constexpr void operator()(const HandshakeOkEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              NetworkContext& ctx) const {
        ctx.session_id = 1001;
        ctx.bytes_transferred += 128;
        std::cout << "\033[1;32m  [NET Action]\033[0m TLS 1.3 Handshake COMPLETE -> Authenticated Session #"
                  << ctx.session_id << " established.\n";
    }
};

struct CleanupAction {
    constexpr void operator()(const auto& /*evt*/, auto& /*src*/, auto& /*dst*/, NetworkContext& ctx) const {
        ctx.socket_fd = -1;
        ctx.session_id = 0;
        ctx.queue_paused = false;
        std::cout << "\033[1;33m  [NET Action]\033[0m Connection terminated -> Socket buffers flushed & resources "
                     "reclaimed.\n";
    }
};

struct PauseQueueAction {
    constexpr void operator()(const NetworkDegradedEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              NetworkContext& ctx) const {
        ctx.queue_paused = true;
        std::cout << "\033[1;33m  [NET Action]\033[0m High packet loss detected (>15%) -> Outbound packet queue "
                     "PAUSED.\n";
    }
};

struct ResumeQueueAction {
    constexpr void operator()(const NetworkRestoredEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              NetworkContext& ctx) const {
        ctx.queue_paused = false;
        std::cout << "\033[1;32m  [NET Action]\033[0m Link latency stabilized (<20ms) -> Outbound packet queue "
                     "RESUMED.\n";
    }
};

struct CloseSocketAction {
    constexpr void operator()(const DisconnectCmd& /*evt*/, auto& /*src*/, auto& /*dst*/, NetworkContext& ctx) const {
        ctx.socket_fd = -1;
        ctx.session_id = 0;
        std::cout << "\033[1;32m  [NET Action]\033[0m FIN-ACK sequence complete -> Socket closed gracefully.\n";
    }
};

}  // namespace net

namespace {

void print_header(std::string_view title) {
    std::cout << "\n\033[1;35m======================================================================\033[0m\n";
    std::cout << "\033[1;37m  " << title << "\033[0m\n";
    std::cout << "\033[1;35m======================================================================\033[0m\n";
}

void print_network_hud(const net::NetworkContext& ctx, std::string_view state_name) {
    std::cout << "  ┌─────────────────────────────────────────────────────────────┐\n";
    std::cout << "  │ \033[1mState:\033[0m " << std::left << std::setw(15) << state_name << " │ \033[1mSocket:\033[0m  "
              << std::left << std::setw(6) << (ctx.socket_fd >= 0 ? ("fd=" + std::to_string(ctx.socket_fd)) : "CLOSED")
              << " │ \033[1mSession:\033[0m " << (ctx.session_id > 0 ? ("#" + std::to_string(ctx.session_id)) : "NONE ")
              << "      │\n";
    std::cout << "  │ \033[1mQueue:\033[0m " << std::left << std::setw(15)
              << (ctx.queue_paused ? "\033[33mPAUSED\033[0m" : "\033[32mACTIVE\033[0m") << " │ \033[1mErrors:\033[0m  "
              << std::left << std::setw(6) << ctx.error_count << " │ \033[1mTransferred:\033[0m "
              << ctx.bytes_transferred << " B    │\n";
    std::cout << "  └─────────────────────────────────────────────────────────────┘\n";
}

}  // namespace

int main() {
    print_header("fsmc Networking Showcase: Resilient Connection Protocol (SysML / C++20)");

    net::NetworkContext context;
    context.has_network_interface = true;
    context.has_valid_credentials = true;

    // 1. Instantiate the State Machine with custom NetworkContext
    net::ConnectionFSM fsm(context);

    // Attach live telemetry observer
    fsm.set_observer([](const fsm::transition_info& info) {
        std::cout << "\033[1;34m[NET OBSERVER]\033[0m " << info.source << " --(\033[1m" << info.event << "\033[0m)--> "
                  << info.target << (info.is_internal() ? " \033[36m[INTERNAL]\033[0m" : "") << "\n";
    });

    // Phase 1: Initial State Verification
    print_header("PHASE 1: Initial State & Hardware Network Interface Verification");
    print_network_hud(fsm.context(), fsm.current_state_name());
    assert(fsm.is_in_state<net::Disconnected>());

    // Phase 2: Establishing Connection (Guarded Composite Transition)
    print_header("PHASE 2: TLS Connection Handshake (Guarded Boolean Transition)");
    std::cout << "\n--> [App] Dispatching ConnectCmd (HasNetwork && HasValidCredentials)...\n";
    auto res = fsm.dispatch(net::ConnectCmd{});
    assert(res.is_success());
    assert(fsm.is_in_state<net::Connecting>());
    print_network_hud(fsm.context(), fsm.current_state_name());

    std::cout << "\n--> [App] Receiving TLS Handshake Certificate validation...\n";
    res = fsm.dispatch(net::HandshakeOkEvent{});
    assert(res.is_success());
    assert(fsm.is_in_state<net::Connected>());
    assert(context.session_id == 1001);
    print_network_hud(fsm.context(), fsm.current_state_name());

    // Phase 3: Network Degradation & Automatic Flow Control
    print_header("PHASE 3: QoS Degradation & Automatic Packet Queue Flow Control");
    std::cout << "\n--> [App] Signal degradation detected (Packet loss alert)...\n";
    res = fsm.dispatch(net::NetworkDegradedEvent{});
    assert(res.is_success());
    assert(fsm.is_in_state<net::Suspended>());
    assert(context.queue_paused);
    print_network_hud(fsm.context(), fsm.current_state_name());

    std::cout << "\n--> [App] Network quality recovered -> Resuming outbound flow...\n";
    res = fsm.dispatch(net::NetworkRestoredEvent{});
    assert(res.is_success());
    assert(fsm.is_in_state<net::Connected>());
    assert(!context.queue_paused);
    print_network_hud(fsm.context(), fsm.current_state_name());

    // Phase 4: Graceful Disconnection
    print_header("PHASE 4: Graceful Session Termination");
    std::cout << "\n--> [App] Dispatching DisconnectCmd...\n";
    res = fsm.dispatch(net::DisconnectCmd{});
    assert(res.is_success());
    assert(fsm.is_in_state<net::Disconnected>());
    print_network_hud(fsm.context(), fsm.current_state_name());

    // Phase 5: De Morgan Boolean Guard Rejection Tests
    print_header("PHASE 5: De Morgan Composite Boolean Guard Enforcement");
    std::cout << "\n  \033[33m[Test 5A]\033[0m Attempting connection without physical network interface:\n";
    {
        net::NetworkContext offline_ctx;
        offline_ctx.has_network_interface = false;
        net::ConnectionFSM offline_fsm(offline_ctx);
        offline_fsm.dispatch(net::ConnectCmd{});
        assert(offline_fsm.is_in_state<net::Disconnected>());
        assert(offline_ctx.error_count == 1);
        std::cout << "  (Connection safely rejected by guard [!HasNetwork || !HasValidCredentials]! State remains: "
                  << offline_fsm.current_state_name() << ")\n";
    }

    std::cout << "\n  \033[33m[Test 5B]\033[0m Attempting connection with invalid cryptographic credentials:\n";
    {
        net::NetworkContext invalid_cred_ctx;
        invalid_cred_ctx.has_valid_credentials = false;
        net::ConnectionFSM cred_fsm(invalid_cred_ctx);
        cred_fsm.dispatch(net::ConnectCmd{});
        assert(cred_fsm.is_in_state<net::Disconnected>());
        assert(invalid_cred_ctx.error_count == 1);
        std::cout << "  (Connection safely rejected by credentials validation guard! State remains: "
                  << cred_fsm.current_state_name() << ")\n";
    }

    // Phase 6: Handshake Timeout Recovery
    print_header("PHASE 6: TCP / TLS Handshake Timeout Recovery");
    {
        net::NetworkContext timeout_ctx;
        net::ConnectionFSM timeout_fsm(timeout_ctx);
        timeout_fsm.dispatch(net::ConnectCmd{});
        std::cout << "  State: " << timeout_fsm.current_state_name() << " -> Simulating 5000ms handshake timeout...\n";
        timeout_fsm.dispatch(net::TimeoutEvent{});
        assert(timeout_fsm.is_in_state<net::Disconnected>());
        assert(timeout_ctx.socket_fd == -1);
        std::cout << "  (Timeout safely triggered cleanup and transitioned back to: "
                  << timeout_fsm.current_state_name() << ")\n";
    }

    // Phase 7: Multithreaded Asynchronous Worker Execution
    print_header("PHASE 7: Asynchronous Background Thread-Safe Worker Execution");
    net::NetworkContext async_ctx;
    net::ThreadSafeConnectionFSM async_fsm(async_ctx);
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

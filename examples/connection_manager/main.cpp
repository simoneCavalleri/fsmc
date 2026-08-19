#include <cassert>
#include <chrono>
#include <iostream>
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
        std::cout << "  [ACTION] Network interface OK -> Socket opened (fd=" << ctx.socket_fd << ").\n";
    }
};

struct LogErrorAction {
    constexpr void operator()(const ConnectCmd& /*evt*/, auto& /*src*/, auto& /*dst*/, NetworkContext& ctx) const {
        ctx.error_count++;
        std::cout << "  [ACTION] Network interface MISSING -> Connection attempt rejected (errors=" << ctx.error_count
                  << ").\n";
    }
};

struct SetupSessionAction {
    constexpr void operator()(const HandshakeOkEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              NetworkContext& ctx) const {
        ctx.session_id = 1001;
        std::cout << "  [ACTION] TLS Handshake SUCCESS -> Session #" << ctx.session_id << " established.\n";
    }
};

struct CleanupAction {
    constexpr void operator()(const auto& /*evt*/, auto& /*src*/, auto& /*dst*/, NetworkContext& ctx) const {
        ctx.socket_fd = -1;
        ctx.session_id = 0;
        ctx.queue_paused = false;
        std::cout << "  [ACTION] Connection terminated -> Buffers flushed & resources cleaned up.\n";
    }
};

struct PauseQueueAction {
    constexpr void operator()(const NetworkDegradedEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              NetworkContext& ctx) const {
        ctx.queue_paused = true;
        std::cout << "  [ACTION] Packet loss detected -> Outbound packet queue PAUSED.\n";
    }
};

struct ResumeQueueAction {
    constexpr void operator()(const NetworkRestoredEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              NetworkContext& ctx) const {
        ctx.queue_paused = false;
        std::cout << "  [ACTION] Network quality restored -> Outbound packet queue RESUMED.\n";
    }
};

struct CloseSocketAction {
    constexpr void operator()(const DisconnectCmd& /*evt*/, auto& /*src*/, auto& /*dst*/, NetworkContext& ctx) const {
        ctx.socket_fd = -1;
        ctx.session_id = 0;
        std::cout << "  [ACTION] Disconnect requested -> Socket closed cleanly.\n";
    }
};

}  // namespace net

// ============================================================================
// Main Showcase Execution
// ============================================================================
int main() {
    std::cout << "======================================================================\n"
              << "       FSMC SHOWCASE: NETWORK CONNECTION MANAGER (C++20 DSL)          \n"
              << "======================================================================\n\n";

    net::NetworkContext context;
    context.has_network_interface = true;

    // 1. Instantiate the State Machine with custom NetworkContext
    net::ConnectionFSM fsm(context);

    std::cout << "[PHASE 1] Initial State Verification\n";
    std::cout << "  Current State: " << fsm.current_state_name() << "\n";
    assert(fsm.is_in_state<net::Disconnected>());
    assert(fsm.current_state_name() == "Disconnected");

    // 2. Establishing Connection (Disconnected -> Connecting -> Connected)
    std::cout << "\n[PHASE 2] Establishing Network Connection (Guarded Transition)\n";
    bool handled = fsm.dispatch(net::ConnectCmd{});
    (void)handled;
    assert(handled);
    assert(fsm.is_in_state<net::Connecting>());
    assert(context.socket_fd == 42);
    std::cout << "  Current State: " << fsm.current_state_name() << "\n";

    handled = fsm.dispatch(net::HandshakeOkEvent{});
    assert(handled);
    assert(fsm.is_in_state<net::Connected>());
    assert(context.session_id == 1001);
    std::cout << "  Current State: " << fsm.current_state_name() << "\n";

    // 3. Network Degradation & Suspension (Connected -> Suspended -> Connected)
    std::cout << "\n[PHASE 3] Handling Network Degradation & Automatic Resume\n";
    handled = fsm.dispatch(net::NetworkDegradedEvent{});
    assert(handled);
    assert(fsm.is_in_state<net::Suspended>());
    assert(context.queue_paused);
    std::cout << "  Current State: " << fsm.current_state_name() << " (Queue paused)\n";

    handled = fsm.dispatch(net::NetworkRestoredEvent{});
    assert(handled);
    assert(fsm.is_in_state<net::Connected>());
    assert(!context.queue_paused);
    std::cout << "  Current State: " << fsm.current_state_name() << " (Queue resumed)\n";

    // 4. Graceful Disconnection (Connected -> Disconnected)
    std::cout << "\n[PHASE 4] Graceful Disconnection Sequence\n";
    handled = fsm.dispatch(net::DisconnectCmd{});
    assert(handled);
    assert(fsm.is_in_state<net::Disconnected>());
    assert(context.socket_fd == -1);
    std::cout << "  Current State: " << fsm.current_state_name() << "\n";

    // 5. Composite Boolean Guard Rejection Tests (De Morgan logic)
    std::cout << "\n[PHASE 5A] Composite Guard Rejection Test (No Network Interface)\n";
    {
        net::NetworkContext offline_ctx;
        offline_ctx.has_network_interface = false;
        offline_ctx.has_valid_credentials = true;
        net::ConnectionFSM offline_fsm(offline_ctx);

        handled = offline_fsm.dispatch(net::ConnectCmd{});
        assert(handled);
        assert(offline_fsm.is_in_state<net::Disconnected>());
        assert(offline_ctx.error_count == 1);
        std::cout << "  Connection safely rejected by composite guard [!HasNetworkGuard || !HasValidCredentialsGuard], "
                     "state: "
                  << offline_fsm.current_state_name() << "\n";
    }

    std::cout << "\n[PHASE 5B] Composite Guard Rejection Test (Network OK but Invalid Credentials)\n";
    {
        net::NetworkContext cred_ctx;
        cred_ctx.has_network_interface = true;
        cred_ctx.has_valid_credentials = false;  // Triggers [!HasNetworkGuard || !HasValidCredentialsGuard]
        net::ConnectionFSM cred_fsm(cred_ctx);

        handled = cred_fsm.dispatch(net::ConnectCmd{});
        assert(handled);
        assert(cred_fsm.is_in_state<net::Disconnected>());
        assert(cred_ctx.error_count == 1);
        std::cout << "  Connection safely rejected by composite guard logic (has_valid_credentials=false), state: "
                  << cred_fsm.current_state_name() << "\n";
    }

    // 6. Timeout Recovery Test (Connecting -> Timeout -> Disconnected)
    std::cout << "\n[PHASE 6] Handshake Timeout Recovery\n";
    {
        net::NetworkContext timeout_ctx;
        timeout_ctx.has_network_interface = true;
        net::ConnectionFSM timeout_fsm(timeout_ctx);

        timeout_fsm.dispatch(net::ConnectCmd{});
        assert(timeout_fsm.is_in_state<net::Connecting>());

        handled = timeout_fsm.dispatch(net::TimeoutEvent{});
        assert(handled);
        assert(timeout_fsm.is_in_state<net::Disconnected>());
        assert(timeout_ctx.socket_fd == -1);
        std::cout << "  Timeout handled successfully, returned to: " << timeout_fsm.current_state_name() << "\n";
    }

    // 7. Asynchronous Thread-Safe Worker Showcase
    std::cout << "\n[PHASE 7] Asynchronous Thread-Safe Dispatch Execution\n";
    net::NetworkContext async_ctx;
    net::ThreadSafeConnectionFSM async_fsm(async_ctx);
    async_fsm.start_worker();

    std::cout << "  Posting asynchronous events from producer thread...\n";
    async_fsm.post(net::ConnectCmd{});
    async_fsm.post(net::HandshakeOkEvent{});

    while (!async_fsm.is_in_state<net::Connected>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "  Async FSM successfully reached Connected state under worker thread.\n";
    async_fsm.stop_worker();

    // 8. Real-Time Transition Observer & Telemetry Hook
    std::cout << "\n[PHASE 8] Real-Time Transition Observer Hook Demonstration\n";
    net::ConnectionFSM observed_fsm(context);
    observed_fsm.set_observer([](const fsm::transition_info& info) {
        std::cout << "  [OBSERVER TRACE] " << info.source << " --(" << info.event << ")--> " << info.target
                  << (info.is_internal ? " [INTERNAL]" : "") << "\n";
    });

    observed_fsm.dispatch(net::ConnectCmd{});
    observed_fsm.dispatch(net::HandshakeOkEvent{});
    observed_fsm.dispatch(net::DisconnectCmd{});

    std::cout << "\n======================================================================\n"
              << "  [SUCCESS] All Connection Manager features demonstrated successfully!\n"
              << "======================================================================\n";
    return 0;
}

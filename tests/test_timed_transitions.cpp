#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "fsm/fsm.hpp"
#include "fsm/thread_safe_fsm.hpp"
#include "fsm/transition.hpp"

namespace {

// ============================================================================
// Synchronous Timed Transition Types
// ============================================================================

struct Connecting {
    static constexpr std::string_view name = "Connecting";
};

struct Connected {
    static constexpr std::string_view name = "Connected";
};

struct Disconnected {
    static constexpr std::string_view name = "Disconnected";
};

struct HandshakeOk {};
using Timeout500ms = fsm::after_ms<500>;

struct TimeoutAction {
    void operator()(const Timeout500ms& /*evt*/, Connecting& /*src*/, Disconnected& /*dst*/) const {}
};

using ConnTable = fsm::transition_table<
    fsm::transition<Connecting, HandshakeOk, Connected>,
    fsm::transition<Connecting, Timeout500ms, Disconnected, TimeoutAction>
>;

void test_sync_timed_event() {
    std::cout << "[TEST] Running test_sync_timed_event...\n";

    fsm::fsm<ConnTable> sm;
    assert(sm.is_in_state<Connecting>());

    // Dispatch synchronous timed event
    bool handled = sm.dispatch(Timeout500ms{});
    assert(handled);
    assert(sm.is_in_state<Disconnected>());

    std::cout << "[PASS] test_sync_timed_event passed.\n";
}

// ============================================================================
// Asynchronous Priority Deadline Scheduling
// ============================================================================

struct Step1 {};
struct Step2 {};
struct Step3 {};

struct StateA { static constexpr std::string_view name = "StateA"; };
struct StateB { static constexpr std::string_view name = "StateB"; };
struct StateC { static constexpr std::string_view name = "StateC"; };
struct StateD { static constexpr std::string_view name = "StateD"; };

struct OrderContext {
    std::vector<std::string> log;
};

struct ActionAtoB {
    void operator()(const Step1& /*evt*/, StateA& /*src*/, StateB& /*dst*/, OrderContext& ctx) const {
        ctx.log.emplace_back("A->B");
    }
};

struct ActionBtoC {
    void operator()(const Step2& /*evt*/, StateB& /*src*/, StateC& /*dst*/, OrderContext& ctx) const {
        ctx.log.emplace_back("B->C");
    }
};

struct ActionCtoD {
    void operator()(const Step3& /*evt*/, StateC& /*src*/, StateD& /*dst*/, OrderContext& ctx) const {
        ctx.log.emplace_back("C->D");
    }
};

using OrderTable = fsm::transition_table<
    fsm::transition<StateA, Step1, StateB, ActionAtoB>,
    fsm::transition<StateB, Step2, StateC, ActionBtoC>,
    fsm::transition<StateC, Step3, StateD, ActionCtoD>
>;

void test_async_post_delayed_priority() {
    std::cout << "[TEST] Running test_async_post_delayed_priority...\n";

    OrderContext ctx;
    fsm::thread_safe_fsm<OrderTable, OrderContext> async_sm(ctx);
    async_sm.start_worker();

    assert(async_sm.is_in_state<StateA>());

    // Post Step3 with 60ms delay, Step2 with 30ms delay, Step1 with 5ms delay
    // Regardless of posting order, execution order must be Step1 (5ms) -> Step2 (30ms) -> Step3 (60ms)
    async_sm.post_delayed(Step3{}, std::chrono::milliseconds(60));
    async_sm.post_delayed(Step2{}, std::chrono::milliseconds(30));
    async_sm.post_delayed(Step1{}, std::chrono::milliseconds(5));

    // Wait until final state is reached
    while (!async_sm.is_in_state<StateD>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    assert(ctx.log.size() == 3);
    assert(ctx.log[0] == "A->B");
    assert(ctx.log[1] == "B->C");
    assert(ctx.log[2] == "C->D");

    async_sm.stop_worker();
    std::cout << "[PASS] test_async_post_delayed_priority passed (Timers executed in chronological priority order).\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "     RUNNING TIMED TRANSITIONS TESTS    \n"
              << "========================================\n";

    test_sync_timed_event();
    test_async_post_delayed_priority();

    std::cout << "========================================\n"
              << "     ALL TIMED TESTS PASSED (2/2)!      \n"
              << "========================================\n";
    return 0;
}

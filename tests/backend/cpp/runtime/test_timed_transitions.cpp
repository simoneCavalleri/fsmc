#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/thread_safe_fsm.hpp"
#include "fsm/backend/cpp/runtime/transition.hpp"

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

using ConnTable = fsm::transition_table<fsm::transition<Connecting, HandshakeOk, Connected>,
                                        fsm::transition<Connecting, Timeout500ms, Disconnected, TimeoutAction>>;

/**
 * @brief Test Intent: Verify synchronous dispatch of compile-time duration timed events (`fsm::after_ms<500>`).
 *
 * Scenario:
 * - Define transition table with `Timeout500ms`.
 * - Dispatch timed event directly and verify transition from Connecting to Disconnected.
 */
TEST(TimedTransitionsTest, SyncTimedEventDispatch) {
    fsm::fsm<ConnTable> sm;
    EXPECT_TRUE(sm.is_in_state<Connecting>());

    // Dispatch synchronous timed event
    auto handled = sm.dispatch(Timeout500ms{});
    EXPECT_TRUE(handled.is_success());
    EXPECT_TRUE(sm.is_in_state<Disconnected>());
}

// ============================================================================
// Asynchronous Priority Deadline Scheduling
// ============================================================================

struct Step1 {};
struct Step2 {};
struct Step3 {};

struct StateA {
    static constexpr std::string_view name = "StateA";
};
struct StateB {
    static constexpr std::string_view name = "StateB";
};
struct StateC {
    static constexpr std::string_view name = "StateC";
};
struct StateD {
    static constexpr std::string_view name = "StateD";
};

struct OrderRegisters {
    std::vector<std::string> log;
};

struct ActionAtoB {
    void operator()(OrderRegisters& reg) const {
        reg.log.emplace_back("A->B");
    }
};

struct ActionBtoC {
    void operator()(OrderRegisters& reg) const {
        reg.log.emplace_back("B->C");
    }
};

struct ActionCtoD {
    void operator()(OrderRegisters& reg) const {
        reg.log.emplace_back("C->D");
    }
};

using OrderTable = fsm::transition_table<fsm::transition<StateA, Step1, StateB, ActionAtoB>,
                                         fsm::transition<StateB, Step2, StateC, ActionBtoC>,
                                         fsm::transition<StateC, Step3, StateD, ActionCtoD>>;

/**
 * @brief Test Intent: Verify chronological priority deadline scheduling with `post_delayed()`.
 *
 * Scenario:
 * - Post Step3 (60ms delay), Step2 (30ms delay), and Step1 (5ms delay) in reverse order.
 * - Verify priority queue executes events in strict chronological order: Step1 -> Step2 -> Step3.
 */
TEST(TimedTransitionsTest, AsyncPostDelayedPriorityChronologicalOrder) {
    OrderRegisters reg;
    fsm::thread_safe_fsm<OrderTable, fsm::no_ports, fsm::no_ports, OrderRegisters> async_sm(reg);
    async_sm.start_worker();

    EXPECT_TRUE(async_sm.is_in_state<StateA>());

    // Post Step3 with 60ms delay, Step2 with 30ms delay, Step1 with 5ms delay
    // Regardless of posting order, execution order must be Step1 (5ms) -> Step2 (30ms) -> Step3 (60ms)
    async_sm.post_delayed(Step3{}, std::chrono::milliseconds(60));
    async_sm.post_delayed(Step2{}, std::chrono::milliseconds(30));
    async_sm.post_delayed(Step1{}, std::chrono::milliseconds(5));

    // Wait until final state is reached
    while (!async_sm.is_in_state<StateD>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    ASSERT_EQ(async_sm.registers().log.size(), 3U);
    EXPECT_EQ(async_sm.registers().log[0], "A->B");
    EXPECT_EQ(async_sm.registers().log[1], "B->C");
    EXPECT_EQ(async_sm.registers().log[2], "C->D");

    async_sm.stop_worker();
}

// ============================================================================
// Reentrancy & Recursive Lock Safety in Action Execution
// ============================================================================

struct ReentrantRegisters;

using ReentrantTable = fsm::transition_table<fsm::transition<StateA, Step1, StateB>,
                                             fsm::transition<StateB, Step2, StateC>>;

using ReentrantSm = fsm::thread_safe_fsm<ReentrantTable, fsm::no_ports, fsm::no_ports, ReentrantRegisters>;

struct ActionSelfPost {
    void operator()(const Step1& /*evt*/, StateA& /*src*/, StateB& /*dst*/, ReentrantRegisters& reg) const;
};

struct ActionFinal {
    void operator()(const Step2& /*evt*/, StateB& /*src*/, StateC& /*dst*/, ReentrantRegisters& reg) const;
};

using ReentrantActionTable = fsm::transition_table<fsm::transition<StateA, Step1, StateB, ActionSelfPost>,
                                                   fsm::transition<StateB, Step2, StateC, ActionFinal>>;

using ReentrantActionSm = fsm::thread_safe_fsm<ReentrantActionTable, fsm::no_ports, fsm::no_ports, ReentrantRegisters>;

struct ReentrantRegisters {
    ReentrantActionSm* sm_ptr = nullptr;
    bool self_post_executed = false;
    bool final_executed = false;
};

void ActionSelfPost::operator()(const Step1& /*evt*/, StateA& /*src*/, StateB& /*dst*/, ReentrantRegisters& reg) const {
    reg.self_post_executed = true;
    // 1. Reentrant query of state name under recursive lock
    ASSERT_NE(reg.sm_ptr, nullptr);
    EXPECT_TRUE(reg.sm_ptr->is_in_state<StateA>());
    // 2. Reentrant self-posting of next event from inside action handler
    reg.sm_ptr->post(Step2{});
}

void ActionFinal::operator()(const Step2& /*evt*/, StateB& /*src*/, StateC& /*dst*/, ReentrantRegisters& reg) const {
    reg.final_executed = true;
}

/**
 * @brief Test Intent: Verify recursive lock safety when actions self-post events to the asynchronous queue.
 *
 * Scenario:
 * - ActionSelfPost is executed on Step1, queries active state, and self-posts Step2 back into the FSM.
 * - Verify no deadlocks or mutex violations occur, reaching StateC smoothly.
 */
TEST(TimedTransitionsTest, AsyncReentrantActionSelfPost) {
    ReentrantRegisters reg;
    ReentrantActionSm async_sm(reg);
    async_sm.registers().sm_ptr = &async_sm;

    async_sm.start_worker();

    // Trigger initial transition
    async_sm.post(Step1{});

    // Wait until background worker processes reentrantly self-posted Step2 event
    while (!async_sm.is_in_state<StateC>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(async_sm.registers().self_post_executed);
    EXPECT_TRUE(async_sm.registers().final_executed);
    EXPECT_TRUE(async_sm.is_in_state<StateC>());

    async_sm.stop_worker();
}

}  // namespace

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "fsm/runtime/cpp/fsm.hpp"
#include "fsm/runtime/cpp/thread_safe_fsm.hpp"
#include "fsm/runtime/cpp/transition.hpp"

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
    OrderContext ctx;
    fsm::thread_safe_fsm<OrderTable, OrderContext> async_sm(ctx);
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

    ASSERT_EQ(ctx.log.size(), 3U);
    EXPECT_EQ(ctx.log[0], "A->B");
    EXPECT_EQ(ctx.log[1], "B->C");
    EXPECT_EQ(ctx.log[2], "C->D");

    async_sm.stop_worker();
}

// ============================================================================
// Reentrancy & Recursive Lock Safety in Action Execution
// ============================================================================

struct ReentrantCtx;

struct ActionSelfPost {
    void operator()(const Step1& /*evt*/, StateA& /*src*/, StateB& /*dst*/, ReentrantCtx& ctx) const;
};

struct ActionFinal {
    void operator()(const Step2& /*evt*/, StateB& /*src*/, StateC& /*dst*/, ReentrantCtx& ctx) const;
};

using ReentrantTable = fsm::transition_table<fsm::transition<StateA, Step1, StateB, ActionSelfPost>,
                                             fsm::transition<StateB, Step2, StateC, ActionFinal>>;

struct ReentrantCtx {
    fsm::thread_safe_fsm<ReentrantTable, ReentrantCtx>* sm_ptr = nullptr;
    bool self_post_executed = false;
    bool final_executed = false;
};

void ActionSelfPost::operator()(const Step1& /*evt*/, StateA& /*src*/, StateB& /*dst*/, ReentrantCtx& ctx) const {
    ctx.self_post_executed = true;
    // 1. Reentrant query of state name under recursive lock
    ASSERT_NE(ctx.sm_ptr, nullptr);
    EXPECT_TRUE(ctx.sm_ptr->is_in_state<StateA>());
    // 2. Reentrant self-posting of next event from inside action handler
    ctx.sm_ptr->post(Step2{});
}

void ActionFinal::operator()(const Step2& /*evt*/, StateB& /*src*/, StateC& /*dst*/, ReentrantCtx& ctx) const {
    ctx.final_executed = true;
}

/**
 * @brief Test Intent: Verify recursive lock safety when actions self-post events to the asynchronous queue.
 *
 * Scenario:
 * - ActionSelfPost is executed on Step1, queries active state, and self-posts Step2 back into the FSM.
 * - Verify no deadlocks or mutex violations occur, reaching StateC smoothly.
 */
TEST(TimedTransitionsTest, AsyncReentrantActionSelfPost) {
    ReentrantCtx ctx;
    fsm::thread_safe_fsm<ReentrantTable, ReentrantCtx> async_sm(ctx);
    ctx.sm_ptr = &async_sm;

    async_sm.start_worker();

    // Trigger initial transition
    async_sm.post(Step1{});

    // Wait until background worker processes reentrantly self-posted Step2 event
    while (!async_sm.is_in_state<StateC>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(ctx.self_post_executed);
    EXPECT_TRUE(ctx.final_executed);
    EXPECT_TRUE(async_sm.is_in_state<StateC>());

    async_sm.stop_worker();
}

}  // namespace

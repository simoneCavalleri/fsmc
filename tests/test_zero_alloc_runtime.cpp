#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "fsm/fsm.hpp"
#include "fsm/static_ring_buffer.hpp"
#include "fsm/static_thread_safe_fsm.hpp"
#include "fsm/type_traits.hpp"

namespace {

struct SampleEvent {
    static constexpr std::string_view name = "SampleEvent";
    int data = 0;
};

struct AnonymousEvt {
    int val = 0;
};

TEST(ZeroAllocRuntimeTest, StaticRingBufferBasicOps) {
    fsm::static_ring_buffer<int, 4> buffer;

    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.full());
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_EQ(buffer.capacity(), 4u);

    EXPECT_TRUE(buffer.push(10));
    EXPECT_TRUE(buffer.push(20));
    EXPECT_TRUE(buffer.push(30));
    EXPECT_TRUE(buffer.push(40));

    EXPECT_TRUE(buffer.full());
    EXPECT_FALSE(buffer.push(50));  // Full!

    EXPECT_EQ(*buffer.peek(), 10);
    auto popped = buffer.pop();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(*popped, 10);

    EXPECT_EQ(buffer.size(), 3u);
    EXPECT_TRUE(buffer.push(50));  // Can push now

    EXPECT_EQ(*buffer.pop(), 20);
    EXPECT_EQ(*buffer.pop(), 30);
    EXPECT_EQ(*buffer.pop(), 40);
    EXPECT_EQ(*buffer.pop(), 50);

    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.pop().has_value());
}

struct StateIdle {};
struct StateRunning {};
struct StateStopped {};

struct EvStart {};
struct EvStop {};
struct EvReset {};

using MinimalTable = fsm::transition_table<fsm::transition<StateIdle, EvStart, StateRunning>,
                                           fsm::transition<StateRunning, EvStop, StateStopped>,
                                           fsm::transition<StateStopped, EvReset, StateIdle>>;

TEST(ZeroAllocRuntimeTest, TrueCompileTimeZeroOverheadSize) {
    using MinimalFSM = fsm::fsm<MinimalTable>;

    // Minimal FSM has NO history, NO deferred events, NO dynamic observer (no_observer policy)
    // Its size is bounded to 32 bytes (state variant + context ptr + observer), eliminating the 88+ bytes of dynamic
    // vectors & std::function
    static_assert(sizeof(MinimalFSM) <= 32);

    MinimalFSM machine;
    EXPECT_TRUE(machine.is_in_state<StateIdle>());

    auto r1 = machine.dispatch(EvStart{});
    EXPECT_TRUE(r1.is_success());
    EXPECT_TRUE(machine.is_in_state<StateRunning>());

    auto r2 = machine.dispatch(EvStop{});
    EXPECT_TRUE(r2.is_success());
    EXPECT_TRUE(machine.is_in_state<StateStopped>());

    auto r3 = machine.dispatch(EvReset{});
    EXPECT_TRUE(r3.is_success());
    EXPECT_TRUE(machine.is_in_state<StateIdle>());
}

TEST(ZeroAllocRuntimeTest, StaticThreadSafeFsmOperations) {
    fsm::static_thread_safe_fsm<MinimalTable, fsm::no_context, 8> static_ts_machine;

    EXPECT_TRUE(static_ts_machine.is_in_state<StateIdle>());
    EXPECT_TRUE(static_ts_machine.is_queue_empty());

    // Post into static ring buffer (0 heap allocations)
    EXPECT_TRUE(static_ts_machine.post(EvStart{}));
    EXPECT_TRUE(static_ts_machine.post(EvStop{}));
    EXPECT_EQ(static_ts_machine.pending_events(), 2u);

    // Process one
    EXPECT_TRUE(static_ts_machine.process_one());
    EXPECT_TRUE(static_ts_machine.is_in_state<StateRunning>());
    EXPECT_EQ(static_ts_machine.pending_events(), 1u);

    // Process remaining
    EXPECT_EQ(static_ts_machine.process_all(), 1u);
    EXPECT_TRUE(static_ts_machine.is_in_state<StateStopped>());
    EXPECT_TRUE(static_ts_machine.is_queue_empty());

    // Send direct synchronous
    auto r_sync = static_ts_machine.send(EvReset{});
    EXPECT_TRUE(r_sync.is_success());
    EXPECT_TRUE(static_ts_machine.is_in_state<StateIdle>());

    // Test worker thread with static ring buffer
    static_ts_machine.start_worker();
    EXPECT_TRUE(static_ts_machine.post(EvStart{}));

    while (!static_ts_machine.is_in_state<StateRunning>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(static_ts_machine.is_in_state<StateRunning>());

    static_ts_machine.stop_worker();
}

}  // namespace

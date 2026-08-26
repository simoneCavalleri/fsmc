#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "fsm/runtime/cpp/fsm.hpp"
#include "fsm/runtime/cpp/static_ring_buffer.hpp"
#include "fsm/runtime/cpp/static_thread_safe_fsm.hpp"
#include "fsm/runtime/cpp/type_traits.hpp"

namespace {

struct SampleEvent {
    static constexpr std::string_view name = "SampleEvent";
    int data = 0;
};

struct AnonymousEvt {
    int val = 0;
};

/**
 * @brief Test Intent: Verify boundary conditions, peek inspection, and FIFO ordering for static_ring_buffer.
 *
 * Scenario:
 * - Push items up to capacity 4.
 * - Verify rejection on overflow.
 * - Inspect head item via peek() without removing.
 * - Pop items and verify exact FIFO order.
 */
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

/**
 * @brief Test Intent: Verify true zero-allocation footprint (sizeof <= 32 bytes) for embedded runtimes.
 *
 * Scenario:
 * - Check compile-time machine size with no_observer policy (no heap vectors or std::function objects).
 * - Dispatch transitions synchronously and verify state progression.
 */
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

/**
 * @brief Test Intent: Verify static_thread_safe_fsm operations with zero dynamic allocations.
 *
 * Scenario:
 * - Post events into fixed static queue.
 * - Process events one-by-one via process_one() and in batch via process_all().
 * - Send synchronous events via send().
 * - Start a background worker thread and verify asynchronous processing without heap allocation.
 */
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

/**
 * @brief Test Intent: Verify mutable peek inspection and buffer clearing for static_ring_buffer.
 *
 * Scenario:
 * - Modify head item in place via mutable peek() pointer.
 * - Call clear() and verify size becomes 0 and empty() returns true.
 */
TEST(ZeroAllocRuntimeTest, StaticRingBufferPeekAndClear) {
    fsm::static_ring_buffer<int, 4> buf;
    EXPECT_EQ(buf.peek(), nullptr);

    EXPECT_TRUE(buf.push(42));
    ASSERT_NE(buf.peek(), nullptr);
    EXPECT_EQ(*buf.peek(), 42);

    // Modify via mutable peek
    *buf.peek() = 99;
    EXPECT_EQ(buf.pop().value_or(0), 99);

    EXPECT_TRUE(buf.push(1));
    EXPECT_TRUE(buf.push(2));
    EXPECT_EQ(buf.size(), 2u);
    buf.clear();
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_TRUE(buf.empty());
}

/**
 * @brief Test Intent: Verify deterministic queue overflow rejection in static_thread_safe_fsm.
 *
 * Scenario:
 * - Instantiate static FSM with capacity 2.
 * - Post 2 events until is_queue_full() is true.
 * - Attempt to post 3rd event and verify post() returns false without exceptions or heap allocation.
 * - Process one event and verify queue accepts subsequent posts.
 */
TEST(ZeroAllocRuntimeTest, StaticThreadSafeFsmQueueOverflowHandling) {
    // Capacity 2 static FSM
    fsm::static_thread_safe_fsm<MinimalTable, fsm::no_context, 2> tiny_fsm;
    EXPECT_FALSE(tiny_fsm.is_queue_full());

    EXPECT_TRUE(tiny_fsm.post(EvStart{}));
    EXPECT_TRUE(tiny_fsm.post(EvStop{}));
    EXPECT_TRUE(tiny_fsm.is_queue_full());

    // Third event must be rejected (returns false, 0 dynamic allocation)
    EXPECT_FALSE(tiny_fsm.post(EvReset{}));
    EXPECT_EQ(tiny_fsm.pending_events(), 2u);

    EXPECT_TRUE(tiny_fsm.process_one());
    EXPECT_FALSE(tiny_fsm.is_queue_full());
    EXPECT_TRUE(tiny_fsm.post(EvReset{}));
}

}  // namespace

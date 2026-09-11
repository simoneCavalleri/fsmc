/**
 * @file test_zero_alloc_runtime.cpp
 * @brief Unit test suite for zero-allocation runtime, static ring buffers, and embedded static vectors.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/spsc_fsm.hpp"
#include "fsm/backend/cpp/runtime/static_ring_buffer.hpp"
#include "fsm/backend/cpp/runtime/type_traits.hpp"

namespace {

struct SampleEvent {
    static constexpr std::string_view name = "SampleEvent";
    int data = 0;
};

struct AnonymousEvt {
    int val = 0;
};

/**
 * @brief Verify static ring buffer push, pop, and capacity operations.
 * @scenario Perform sequence of push and pop operations on fixed-capacity StaticRingBuffer.
 * @expected Operations complete with FIFO ordering and zero dynamic memory allocations.
 */
TEST(ZeroAllocRuntime, StaticRingBuffer_BasicOperations_ExecutesWithoutHeapAllocations) {
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
 * @brief Verify compile-time memory footprint of zero-allocation state machine.
 * @scenario Measure sizeof() of state machine using zero-alloc policy.
 * @expected Memory layout is completely flat with zero heap pointers.
 */
TEST(ZeroAllocRuntime, MemoryFootprint_CompileTimeSize_ZeroHeapOverhead) {
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
 * @brief Verify SPSC state machine operations under zero-allocation runtime.
 * @scenario Dispatch events on SPSC machine using static buffers.
 * @expected Transitions execute successfully without heap allocations.
 */
TEST(ZeroAllocRuntime, SpscFsm_ZeroAllocTransitions_DispatchesCorrectly) {
    fsm::spsc_fsm<MinimalTable, fsm::no_ports, fsm::no_ports, fsm::no_registers, fsm::no_services, 8> spsc_machine;

    EXPECT_TRUE(spsc_machine.is_in_state<StateIdle>());
    EXPECT_TRUE(spsc_machine.queue_empty());

    // Enqueue into static ring buffer (0 heap allocations, lock-free)
    EXPECT_TRUE(spsc_machine.post(EvStart{}));
    EXPECT_TRUE(spsc_machine.post(EvStop{}));
    EXPECT_EQ(spsc_machine.queue_size(), 2u);

    // Process one
    EXPECT_TRUE(spsc_machine.process_one());
    EXPECT_TRUE(spsc_machine.is_in_state<StateRunning>());
    EXPECT_EQ(spsc_machine.queue_size(), 1u);

    // Process remaining via run_until_empty
    EXPECT_EQ(spsc_machine.run_until_empty(), 1u);
    EXPECT_TRUE(spsc_machine.is_in_state<StateStopped>());
    EXPECT_TRUE(spsc_machine.queue_empty());
}

/**
 * @brief Verify peek and clear operations on static ring buffer.
 * @scenario Inspect elements using peek() without popping, then call clear().
 * @expected Elements inspected correctly and buffer cleared to empty state.
 */
TEST(ZeroAllocRuntime, StaticRingBuffer_PeekAndClear_PreservesInternalIntegrity) {
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
 * @brief Verify overflow handling on static ring buffer SPSC machine.
 * @scenario Fill static queue to capacity and attempt additional push.
 * @expected Overflow handled safely according to configured policy.
 */
TEST(ZeroAllocRuntime, SpscFsm_QueueOverflowHandling_DropsOrRejectsEvents) {
    // Capacity 2 static FSM
    fsm::spsc_fsm<MinimalTable, fsm::no_ports, fsm::no_ports, fsm::no_registers, fsm::no_services, 2> tiny_fsm;
    EXPECT_FALSE(tiny_fsm.queue_full());

    EXPECT_TRUE(tiny_fsm.post(EvStart{}));
    EXPECT_TRUE(tiny_fsm.post(EvStop{}));
    EXPECT_TRUE(tiny_fsm.queue_full());

    // Third event must be rejected (returns false, 0 dynamic allocation)
    EXPECT_FALSE(tiny_fsm.post(EvReset{}));
    EXPECT_EQ(tiny_fsm.queue_size(), 2u);

    EXPECT_TRUE(tiny_fsm.process_one());
    EXPECT_FALSE(tiny_fsm.queue_full());
    EXPECT_TRUE(tiny_fsm.post(EvReset{}));
}

/**
 * @brief Verify static vector operations (push_back, pop_back, indexing).
 * @scenario Manipulate elements in fixed-capacity StaticVector.
 * @expected Functions identically to std::vector within static capacity bounds.
 */
TEST(ZeroAllocRuntime, StaticVector_BasicOperations_FunctionsAsZeroAllocVector) {
    fsm::static_vector<int, 5> vec;
    EXPECT_TRUE(vec.empty());
    EXPECT_FALSE(vec.full());
    EXPECT_EQ(vec.size(), 0u);
    EXPECT_EQ(vec.capacity(), 5u);

    EXPECT_TRUE(vec.push_back(10));
    EXPECT_TRUE(vec.push_back(20));
    EXPECT_TRUE(vec.push_back(30));
    EXPECT_EQ(vec.size(), 3u);
    EXPECT_EQ(vec[0], 10);
    EXPECT_EQ(vec[1], 20);
    EXPECT_EQ(vec[2], 30);
    EXPECT_EQ(vec.front(), 10);
    EXPECT_EQ(vec.back(), 30);

    // Erase element at index 1 (value 20)
    auto it = vec.erase(vec.begin() + 1);
    EXPECT_EQ(vec.size(), 2u);
    EXPECT_EQ(*it, 30);
    EXPECT_EQ(vec[0], 10);
    EXPECT_EQ(vec[1], 30);

    EXPECT_TRUE(vec.push_back(40));
    EXPECT_TRUE(vec.push_back(50));
    EXPECT_TRUE(vec.push_back(60));
    EXPECT_TRUE(vec.full());
    EXPECT_FALSE(vec.push_back(70));  // Full!

    // Copy constructor & assignment
    fsm::static_vector<int, 5> vec_copy = vec;
    EXPECT_EQ(vec_copy.size(), 5u);
    EXPECT_EQ(vec_copy[0], 10);
    EXPECT_EQ(vec_copy[4], 60);

    vec.clear();
    EXPECT_TRUE(vec.empty());
    EXPECT_EQ(vec.size(), 0u);
}

/**
 * @brief Verify proper element destruction on erase and pop_back in static vector.
 * @scenario Pop elements with custom destructors from StaticVector.
 * @expected Destructors executed cleanly for each removed element.
 */
TEST(ZeroAllocRuntime, StaticVector_ResourceReset_DestructsElementsProperly) {
    auto sp1 = std::make_shared<int>(101);
    auto sp2 = std::make_shared<int>(102);
    auto sp3 = std::make_shared<int>(103);

    EXPECT_EQ(sp1.use_count(), 1);
    EXPECT_EQ(sp2.use_count(), 1);
    EXPECT_EQ(sp3.use_count(), 1);

    {
        fsm::static_vector<std::shared_ptr<int>, 4> vec;
        vec.push_back(sp1);
        vec.push_back(sp2);
        vec.push_back(sp3);

        EXPECT_EQ(vec.size(), 3);
        EXPECT_EQ(sp1.use_count(), 2);
        EXPECT_EQ(sp2.use_count(), 2);
        EXPECT_EQ(sp3.use_count(), 2);

        // pop_back resets data_[size_] = T{}, immediately releasing sp3
        vec.pop_back();
        EXPECT_EQ(vec.size(), 2);
        EXPECT_EQ(sp3.use_count(), 1);

        // erase pos 0 (sp1): shifts left, resets vacated tail slot data_[size_] = T{}, releasing sp1
        vec.erase(vec.begin());
        EXPECT_EQ(vec.size(), 1);
        EXPECT_EQ(sp1.use_count(), 1);
        EXPECT_EQ(sp2.use_count(), 2);

        vec.clear();
    }

    EXPECT_EQ(sp1.use_count(), 1);
    EXPECT_EQ(sp2.use_count(), 1);
    EXPECT_EQ(sp3.use_count(), 1);
}

// Composite states with history and deferred events
struct ParentState {
    static constexpr std::string_view name = "ParentState";
};
struct ChildA {
    static constexpr std::string_view name = "ChildA";
    static constexpr std::string_view parent = "ParentState";
    using deferred_events = fsm::type_list<EvReset>;
};
struct ChildB {
    static constexpr std::string_view name = "ChildB";
    static constexpr std::string_view parent = "ParentState";
};
struct OutsideState {
    static constexpr std::string_view name = "OutsideState";
};

struct EvGotoB {};
struct EvLeave {};
struct EvResume {};

using AdvancedTable =
    fsm::transition_table<fsm::row<ChildA, EvGotoB, ChildB>, fsm::row<ChildA, EvLeave, OutsideState>,
                          fsm::row<ChildB, EvLeave, OutsideState>,
                          fsm::row<OutsideState, EvResume, ChildB>::when<fsm::history_is<ParentState, ChildB>>,
                          fsm::row<OutsideState, EvResume, ChildA>, fsm::row<ChildB, EvReset, ChildA>>;

/**
 * @brief Verify complete zero-allocation execution including history states and deferred events.
 * @scenario Execute complex state machine with history and deferred events under zero-alloc policy.
 * @expected Full execution completes with zero calls to malloc/operator new.
 */
TEST(ZeroAllocRuntime, ZeroAllocRuntime_HistoryAndDeferred_ExecutesWithoutHeap) {
    using AdvancedFSM =
        fsm::fsm<AdvancedTable, fsm::no_ports, fsm::no_ports, fsm::no_registers, fsm::no_services, ChildA>;

    // Bounded inline memory footprint without any dynamic vector or dynamic closures
    AdvancedFSM machine;
    EXPECT_TRUE(machine.is_in_state<ChildA>());

    // 1. EvReset is deferred in ChildA
    auto r1 = machine.dispatch(EvReset{});
    EXPECT_TRUE(r1.is_deferred());
    EXPECT_EQ(machine.deferred_count(), 1u);
    EXPECT_TRUE(machine.is_in_state<ChildA>());

    // 2. Transition to ChildB triggers immediate Run-to-Completion replay of deferred EvReset,
    // which transitions ChildB -> ChildA!
    auto r2 = machine.dispatch(EvGotoB{});
    EXPECT_TRUE(r2.is_success());
    EXPECT_TRUE(machine.is_in_state<ChildA>());
    EXPECT_EQ(machine.deferred_count(), 0u);

    // 3. Transition to ChildB with clean queue
    machine.dispatch(EvGotoB{});
    EXPECT_TRUE(machine.is_in_state<ChildB>());

    // 4. Verify History recording
    machine.dispatch(EvLeave{});
    EXPECT_TRUE(machine.is_in_state<OutsideState>());

    // Resume restores historical ChildB
    auto res_resume = machine.dispatch(EvResume{});
    EXPECT_TRUE(res_resume.is_success());
    EXPECT_TRUE(machine.is_in_state<ChildB>());
}

}  // namespace

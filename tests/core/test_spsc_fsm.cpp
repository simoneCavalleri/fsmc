#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>
#include <vector>

#include "fsm/runtime/cpp/fsm.hpp"
#include "fsm/runtime/cpp/spsc_fsm.hpp"

namespace {

// ============================================================================
// Test Fixtures & Types
// ============================================================================

struct StateIdle {
    static constexpr std::string_view name = "StateIdle";
};
struct StateActive {
    static constexpr std::string_view name = "StateActive";
};
struct StatePaused {
    static constexpr std::string_view name = "StatePaused";
};

struct EvStart {
    static constexpr std::string_view name = "EvStart";
};
struct EvPause {
    static constexpr std::string_view name = "EvPause";
};
struct EvResume {
    static constexpr std::string_view name = "EvResume";
};
struct EvStop {
    static constexpr std::string_view name = "EvStop";
};

struct SampleContext {
    uint64_t counter1 = 0;
    uint64_t counter2 = 0;
};

struct IncrementAction {
    template <typename Event, typename Src, typename Dst>
    void operator()(const Event& /*evt*/, Src& /*src*/, Dst& /*dst*/, SampleContext& ctx) const {
        ++ctx.counter1;
        ++ctx.counter2;
    }
};

using SpscTestTable = fsm::transition_table<fsm::transition<StateIdle, EvStart, StateActive, IncrementAction>,
                                            fsm::transition<StateActive, EvPause, StatePaused, IncrementAction>,
                                            fsm::transition<StatePaused, EvResume, StateActive, IncrementAction>,
                                            fsm::transition<StateActive, EvStop, StateIdle, IncrementAction>,
                                            fsm::transition<StatePaused, EvStop, StateIdle, IncrementAction>>;

/**
 * @brief Test Intent: Verify compile-time introspection on spsc_fsm.
 */
TEST(SpscFsmTest, CompileTimeIntrospection) {
    using Machine = fsm::spsc_fsm<SpscTestTable, SampleContext, 128>;

    static_assert(Machine::state_count == 3);
    static_assert(Machine::transition_count == 5);
    static_assert(Machine::event_count == 4);
    static_assert(Machine::queue_capacity == 128);

    static_assert(Machine::has_state<StateIdle>);
    static_assert(Machine::has_state<StateActive>);
    static_assert(Machine::has_state<StatePaused>);
    static_assert(Machine::has_event<EvStart>);
    static_assert(Machine::has_event<EvStop>);
}

/**
 * @brief Test Intent: Verify basic SPSC execution across distinct producer and consumer threads.
 */
TEST(SpscFsmTest, BasicProducerConsumerExecution) {
    SampleContext ctx;
    fsm::spsc_fsm<SpscTestTable, SampleContext, 64> machine(ctx);

    EXPECT_TRUE(machine.is_in_state<StateIdle>());
    EXPECT_EQ(machine.state_name(), "StateIdle");

    // Producer thread enqueues sequence of events
    std::thread producer([&]() {
        EXPECT_TRUE(machine.enqueue(EvStart{}));
        EXPECT_TRUE(machine.enqueue(EvPause{}));
        EXPECT_TRUE(machine.enqueue(EvResume{}));
        EXPECT_TRUE(machine.enqueue(EvStop{}));
    });
    producer.join();

    EXPECT_EQ(machine.queue_size(), 4u);

    // Consumer drains events
    EXPECT_EQ(machine.run_until_empty(), 4u);
    EXPECT_TRUE(machine.is_in_state<StateIdle>());
    EXPECT_EQ(ctx.counter1, 4u);
    EXPECT_EQ(ctx.counter2, 4u);
}

/**
 * @brief Test Intent: Verify lock-free concurrent reads while consumer executes transitions.
 */
TEST(SpscFsmTest, ConcurrentLockFreeReads) {
    SampleContext ctx;
    fsm::spsc_fsm<SpscTestTable, SampleContext, 1024> machine(ctx);

    std::atomic<bool> running{true};
    constexpr int kIterations = 10000;

    // Reader thread 1: inspects state_name and state_index
    std::thread reader([&]() {
        while (running.load(std::memory_order_relaxed)) {
            auto idx = machine.state_index();
            EXPECT_LE(idx, 2u);
            auto name = machine.state_name();
            EXPECT_FALSE(name.empty());
        }
    });

    // Reader thread 2: takes seqlock snapshots of Context
    std::thread context_reader([&]() {
        while (running.load(std::memory_order_relaxed)) {
            auto snap = machine.snapshot_context();
            // Invariant: counter1 and counter2 are always equal in a consistent snapshot
            EXPECT_EQ(snap.counter1, snap.counter2);
        }
    });

    // Producer + Consumer execution
    for (int i = 0; i < kIterations; ++i) {
        machine.enqueue(EvStart{});
        machine.process_one();
        machine.enqueue(EvStop{});
        machine.process_one();
    }

    running.store(false, std::memory_order_release);
    reader.join();
    context_reader.join();

    EXPECT_TRUE(machine.is_in_state<StateIdle>());
    EXPECT_EQ(ctx.counter1, static_cast<uint64_t>(kIterations * 2));
    EXPECT_EQ(ctx.counter2, static_cast<uint64_t>(kIterations * 2));
}

}  // namespace

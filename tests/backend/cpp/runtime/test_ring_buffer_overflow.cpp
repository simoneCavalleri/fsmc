#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/spsc_fsm.hpp"
#include "fsm/backend/cpp/runtime/static_ring_buffer.hpp"

using namespace fsm;

TEST(RingBufferOverflowTest, DropIncomingPolicy) {
    static_ring_buffer<int, 3> rb;
    EXPECT_TRUE(rb.push(10, OverflowPolicy::DropIncoming));
    EXPECT_TRUE(rb.push(20, OverflowPolicy::DropIncoming));
    EXPECT_TRUE(rb.push(30, OverflowPolicy::DropIncoming));
    EXPECT_TRUE(rb.full());

    // 4th push should be rejected
    EXPECT_FALSE(rb.push(40, OverflowPolicy::DropIncoming));
    EXPECT_EQ(rb.size(), 3);

    // Order should remain 10, 20, 30
    auto v1 = rb.pop();
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(*v1, 10);

    auto v2 = rb.pop();
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v2, 20);

    auto v3 = rb.pop();
    ASSERT_TRUE(v3.has_value());
    EXPECT_EQ(*v3, 30);

    EXPECT_TRUE(rb.empty());
}

TEST(RingBufferOverflowTest, DropOldestPolicy) {
    static_ring_buffer<int, 3> rb;
    EXPECT_TRUE(rb.push(10, OverflowPolicy::DropOldest));
    EXPECT_TRUE(rb.push(20, OverflowPolicy::DropOldest));
    EXPECT_TRUE(rb.push(30, OverflowPolicy::DropOldest));
    EXPECT_TRUE(rb.full());

    // 4th push should drop 10 and add 40
    EXPECT_TRUE(rb.push(40, OverflowPolicy::DropOldest));
    EXPECT_EQ(rb.size(), 3);

    // 5th push should drop 20 and add 50
    EXPECT_TRUE(rb.push(50, OverflowPolicy::DropOldest));
    EXPECT_EQ(rb.size(), 3);

    // Expected contents: 30, 40, 50
    auto v1 = rb.pop();
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(*v1, 30);

    auto v2 = rb.pop();
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v2, 40);

    auto v3 = rb.pop();
    ASSERT_TRUE(v3.has_value());
    EXPECT_EQ(*v3, 50);

    EXPECT_TRUE(rb.empty());
}

// Simple test FSM for spsc_fsm
struct StateA {
    static constexpr std::string_view name() noexcept { return "StateA"; }
};
struct StateB {
    static constexpr std::string_view name() noexcept { return "StateB"; }
};

struct EvToggle {
    static constexpr std::string_view name() noexcept { return "EvToggle"; }
};

using TestFsmTable =
    ::fsm::transition_table<::fsm::transition<StateA, EvToggle, StateB>, ::fsm::transition<StateB, EvToggle, StateA>>;

TEST(SpscFsmTest, QueueOverflowRejection) {
    ::fsm::spsc_fsm<TestFsmTable, ::fsm::no_ports, ::fsm::no_ports, ::fsm::no_registers, ::fsm::no_services, 2> machine;

    // Enqueue 2 events (capacity 2 is full)
    EXPECT_TRUE(machine.enqueue(EvToggle{}));
    EXPECT_TRUE(machine.enqueue(EvToggle{}));
    EXPECT_TRUE(machine.queue_full());

    // 3rd push rejected in wait-free O(1) time
    EXPECT_FALSE(machine.enqueue(EvToggle{}));

    EXPECT_EQ(machine.queue_size(), 2);
    EXPECT_TRUE(machine.process_one());
    EXPECT_TRUE(machine.process_one());
    EXPECT_FALSE(machine.process_one());
}

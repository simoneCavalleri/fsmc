#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "fsm/runtime/cpp/spsc_ring_buffer.hpp"

namespace {

/**
 * @brief Test Intent: Verify single-threaded SPSC ring buffer FIFO semantics and capacity boundaries.
 *
 * Scenario:
 * - Push items until capacity is reached and verify queue reports full.
 * - Attempt to push beyond capacity and verify rejection.
 * - Pop all items and verify exact FIFO order and empty queue status.
 */
TEST(SpscRingBufferTest, SingleThreadBasicOps) {
    fsm::spsc_ring_buffer<int, 8> queue;

    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.full());
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_EQ(queue.capacity(), 8u);

    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(queue.push(i));
    }

    EXPECT_TRUE(queue.full());
    EXPECT_FALSE(queue.push(999));

    for (int i = 0; i < 8; ++i) {
        int val = -1;
        EXPECT_TRUE(queue.pop(val));
        EXPECT_EQ(val, i);
    }

    EXPECT_TRUE(queue.empty());
}

/**
 * @brief Test Intent: Stress-test SPSC ring buffer under high-throughput concurrent multi-threading.
 *
 * Scenario:
 * - One producer thread continuously pushes 100,000 sequenced integers.
 * - One consumer thread continuously pops items into a consumed collection.
 * - Verify all 100,000 items are received in exact sequential order without data races or dropped elements.
 */
TEST(SpscRingBufferTest, MultiThreadedConcurrentStress) {
    constexpr std::size_t TotalItems = 100000;
    fsm::spsc_ring_buffer<std::size_t, 1024> queue;

    std::atomic<bool> start{false};
    std::vector<std::size_t> consumed;
    consumed.reserve(TotalItems);

    std::thread producer([&]() {
        while (!start.load(std::memory_order_relaxed)) {
        }
        for (std::size_t i = 0; i < TotalItems; ++i) {
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        while (!start.load(std::memory_order_relaxed)) {
        }
        while (consumed.size() < TotalItems) {
            std::size_t val = 0;
            if (queue.pop(val)) {
                consumed.push_back(val);
            } else {
                std::this_thread::yield();
            }
        }
    });

    start.store(true, std::memory_order_release);
    producer.join();
    consumer.join();

    ASSERT_EQ(consumed.size(), TotalItems);
    for (std::size_t i = 0; i < TotalItems; ++i) {
        EXPECT_EQ(consumed[i], i);
    }
}

struct Tracker {
    static inline int live_count = 0;
    int id = 0;
    std::string tag;

    Tracker() = default;
    Tracker(int id_val, std::string tag_val) : id(id_val), tag(std::move(tag_val)) { ++live_count; }
    Tracker(const Tracker& o) : id(o.id), tag(o.tag) {
        if (id != 0)
            ++live_count;
    }
    Tracker(Tracker&& o) noexcept : id(o.id), tag(std::move(o.tag)) { o.id = 0; }
    Tracker& operator=(const Tracker& o) = default;
    Tracker& operator=(Tracker&& o) noexcept {
        if (this != &o) {
            id = o.id;
            tag = std::move(o.tag);
            o.id = 0;
        }
        return *this;
    }
    ~Tracker() {
        if (id != 0)
            --live_count;
    }
};

/**
 * @brief Test Intent: Verify exact constructor and destructor lifecycle management for non-trivial objects.
 *
 * Scenario:
 * - Emplace objects with multi-argument constructors into ring buffer.
 * - Pop objects and verify live instance count updates with exact 1-to-1 parity.
 * - Destroy the ring buffer and verify remaining slotted elements are cleanly destroyed with 0 leaks.
 */
TEST(SpscRingBufferTest, NonTrivialObjectLifecyclesAndEmplace) {
    Tracker::live_count = 0;
    {
        fsm::spsc_ring_buffer<Tracker, 8> q;
        EXPECT_TRUE(q.emplace(101, "item1"));
        EXPECT_TRUE(q.emplace(102, "item2"));
        EXPECT_EQ(Tracker::live_count, 2);

        auto popped = q.pop();
        ASSERT_TRUE(popped.has_value());
        EXPECT_EQ(popped->id, 101);
        EXPECT_EQ(popped->tag, "item1");

        // After popping and destroying local copy, count reflects queue contents
        popped.reset();
        EXPECT_EQ(Tracker::live_count, 1);
        // Destroying q will drain the remaining item2
    }
    EXPECT_EQ(Tracker::live_count, 0);
}

}  // namespace

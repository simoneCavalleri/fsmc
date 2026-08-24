#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "fsm/spsc_ring_buffer.hpp"

namespace {

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

}  // namespace

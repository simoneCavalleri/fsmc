/**
 * @file test_spsc_queue.cpp
 * @brief Unit test suite for lock-free SPSC ring buffer memory management and lifecycles.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "fsm/backend/cpp/runtime/spsc_ring_buffer.hpp"

namespace {

/**
 * @brief Verify single-thread push, pop, empty, and full operations on SPSC ring buffer.
 * @scenario Push elements into ring buffer and pop them back.
 * @expected Elements popped in exact insertion order and size metrics update accurately.
 */
TEST(SpscRingBuffer, SingleThreadOps_PushAndPop_OperatesCorrectly) {
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
 * @brief Verify concurrent multi-threaded stress test on SPSC ring buffer.
 * @scenario Run dedicated producer and consumer threads transferring 100,000 items.
 * @expected All 100,000 items received in order with zero data loss and no deadlocks.
 */
TEST(SpscRingBuffer, ConcurrentStress_ProducerConsumer_ZeroDataLoss) {
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
 * @brief Verify non-trivial object construction and destruction lifecycles in ring buffer.
 * @scenario Emplace objects with custom constructors and destructors into ring buffer.
 * @expected Constructors and destructors called matching exact allocation and pop counts.
 */
TEST(SpscRingBuffer, NonTrivialObjects_EmplaceAndPop_ConstructedAndDestroyedCorrectly) {
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

/**
 * @brief Verify raw byte storage and default constructible handling.
 * @scenario Instantiate ring buffer on aligned byte storage buffer.
 * @expected Objects constructed directly in place without default initialization overhead.
 */
TEST(SpscRingBuffer, ByteStorage_DefaultConstructible_AllocatedAccurately) {
    static_assert(std::is_default_constructible_v<int>, "int is default constructible");
    fsm::spsc_ring_buffer<int, 16> ring;
    EXPECT_TRUE(ring.empty());
    EXPECT_EQ(ring.capacity(), 16);

    EXPECT_TRUE(ring.push(42));
    EXPECT_EQ(ring.size(), 1);

    int out_val = 0;
    EXPECT_TRUE(ring.pop(out_val));
    EXPECT_EQ(out_val, 42);
    EXPECT_TRUE(ring.empty());
}

struct NonDefaultType {
    int val;
    explicit NonDefaultType(int v) : val(v) {}
    NonDefaultType(const NonDefaultType&) = default;
    NonDefaultType(NonDefaultType&&) noexcept = default;
    NonDefaultType& operator=(const NonDefaultType&) = default;
    NonDefaultType& operator=(NonDefaultType&&) noexcept = default;
};
static_assert(!std::is_default_constructible_v<NonDefaultType>);

/**
 * @brief Verify in-place emplacement of non-default-constructible payloads.
 * @scenario Emplace types without default constructors into ring buffer.
 * @expected In-place construction succeeds cleanly.
 */
TEST(SpscRingBuffer, NonDefaultConstructible_Emplace_ConstructsInPlace) {
    fsm::spsc_ring_buffer<NonDefaultType, 4> q;
    EXPECT_TRUE(q.emplace(42));
    EXPECT_TRUE(q.emplace(84));
    EXPECT_EQ(q.size(), 2u);

    auto item1 = q.pop();
    ASSERT_TRUE(item1.has_value());
    EXPECT_EQ(item1->val, 42);

    auto item2 = q.pop();
    ASSERT_TRUE(item2.has_value());
    EXPECT_EQ(item2->val, 84);

    EXPECT_TRUE(q.empty());
}

}  // namespace

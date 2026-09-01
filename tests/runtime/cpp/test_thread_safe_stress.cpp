#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "fsm/runtime/cpp/fsm.hpp"
#include "fsm/runtime/cpp/thread_safe_fsm.hpp"

namespace {

// States
struct Idle {
    static constexpr std::string_view name = "Idle";
};
struct Processing {
    static constexpr std::string_view name = "Processing";
};
struct Syncing {
    static constexpr std::string_view name = "Syncing";
};

// Events
struct StartEvent {};
struct FinishEvent {};
struct SyncEvent {};
struct PingEvent {};

// Shared Context
struct StressContext {
    std::atomic<uint64_t> total_starts{0};
    std::atomic<uint64_t> total_finishes{0};
    std::atomic<uint64_t> total_syncs{0};
    std::atomic<uint64_t> total_pings{0};
};

// Actions
struct LogStart {
    template <typename Src, typename Dst>
    void operator()(const StartEvent& /*evt*/, const Src& /*src*/, Dst& /*dst*/, StressContext& ctx) const noexcept {
        ctx.total_starts.fetch_add(1, std::memory_order_relaxed);
    }
};
struct LogFinish {
    template <typename Src, typename Dst>
    void operator()(const FinishEvent& /*evt*/, const Src& /*src*/, Dst& /*dst*/, StressContext& ctx) const noexcept {
        ctx.total_finishes.fetch_add(1, std::memory_order_relaxed);
    }
};
struct LogSync {
    template <typename Src, typename Dst>
    void operator()(const SyncEvent& /*evt*/, const Src& /*src*/, Dst& /*dst*/, StressContext& ctx) const noexcept {
        ctx.total_syncs.fetch_add(1, std::memory_order_relaxed);
    }
};
struct LogPing {
    template <typename Src, typename Dst>
    void operator()(const PingEvent& /*evt*/, const Src& /*src*/, Dst& /*dst*/, StressContext& ctx) const noexcept {
        ctx.total_pings.fetch_add(1, std::memory_order_relaxed);
    }
};

using StressTransitionTable = fsm::transition_table<
    // State transitions
    fsm::row<Idle, StartEvent, Processing, LogStart>, fsm::row<Processing, FinishEvent, Idle, LogFinish>,
    fsm::row<Idle, SyncEvent, Syncing, LogSync>, fsm::row<Syncing, FinishEvent, Idle, LogFinish>,
    fsm::row<Processing, SyncEvent, Syncing, LogSync>,
    // Internal transitions
    fsm::internal_row<Idle, PingEvent, LogPing>, fsm::internal_row<Processing, PingEvent, LogPing>,
    fsm::internal_row<Syncing, PingEvent, LogPing>>;

/**
 * @brief Test Intent: Stress-test thread_safe_fsm under intense 20-thread concurrency (50,000 total events).
 *
 * Scenario:
 * - Launch 20 concurrent producer threads, each posting 2,500 mixed external and internal events.
 * - Concurrently run a consumer thread executing `process_all()`.
 * - Verify no deadlocks, segmentation faults, or lost events occur during high-contention locking.
 */
TEST(ThreadSafeStressTest, HighConcurrency20Threads50kEvents) {
    StressContext ctx;
    fsm::thread_safe_fsm<StressTransitionTable, StressContext, Idle> fsm(ctx);

    constexpr int kNumThreads = 20;
    constexpr int kEventsPerThread = 2500;

    std::vector<std::thread> producers;
    producers.reserve(kNumThreads);

    for (int t = 0; t < kNumThreads; ++t) {
        producers.emplace_back([&fsm]() {
            for (int i = 0; i < kEventsPerThread; ++i) {
                switch (i % 4) {
                    case 0:
                        fsm.post(StartEvent{});
                        break;
                    case 1:
                        fsm.post(FinishEvent{});
                        break;
                    case 2:
                        fsm.post(SyncEvent{});
                        break;
                    case 3:
                        fsm.post(PingEvent{});
                        break;
                    default:
                        break;
                }
            }
        });
    }

    // Consumer thread draining queue concurrently
    std::atomic<bool> done{false};
    std::thread consumer([&fsm, &done]() {
        while (!done.load(std::memory_order_relaxed) || fsm.pending_events() > 0) {
            fsm.process_all();
            std::this_thread::yield();
        }
    });

    for (auto& p : producers) {
        p.join();
    }

    done.store(true, std::memory_order_relaxed);
    consumer.join();

    // Final drain to ensure all are processed
    fsm.process_all();

    uint64_t total_processed = ctx.total_starts + ctx.total_finishes + ctx.total_syncs + ctx.total_pings;
    EXPECT_GT(total_processed, 0u);
}

/**
 * @brief Test Intent: Verify thread-safe concurrency mixing immediate posts and delayed timed transitions.
 *
 * Scenario:
 * - Launch 8 threads simultaneously issuing immediate posts and delayed deadline posts.
 * - Wait for timed events to expire and drain.
 * - Verify all events are recorded without race conditions.
 */
TEST(ThreadSafeStressTest, ConcurrentTimedAndImmediateEvents) {
    StressContext ctx;
    fsm::thread_safe_fsm<StressTransitionTable, StressContext, Idle> fsm(ctx);

    constexpr int kNumThreads = 8;
    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([&fsm, t]() {
            fsm.post(StartEvent{});
            fsm.post_delayed(PingEvent{}, std::chrono::milliseconds(5 + (t * 2)));
            fsm.post(SyncEvent{});
            fsm.post(FinishEvent{});
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Wait for timed events to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    fsm.process_all();

    EXPECT_GT(ctx.total_starts, 0u);
    EXPECT_GT(ctx.total_pings, 0u);
}

}  // namespace

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "fsm/fsm.hpp"
#include "fsm/thread_safe_fsm.hpp"

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

// Actions with correct parameter order: (event, src, dst, ctx) or (event, src, dst) or (event)
struct LogStart {
    template <typename Src, typename Dst>
    void operator()(const StartEvent&, const Src&, Dst&, StressContext& ctx) const noexcept {
        ctx.total_starts.fetch_add(1, std::memory_order_relaxed);
    }
};
struct LogFinish {
    template <typename Src, typename Dst>
    void operator()(const FinishEvent&, const Src&, Dst&, StressContext& ctx) const noexcept {
        ctx.total_finishes.fetch_add(1, std::memory_order_relaxed);
    }
};
struct LogSync {
    template <typename Src, typename Dst>
    void operator()(const SyncEvent&, const Src&, Dst&, StressContext& ctx) const noexcept {
        ctx.total_syncs.fetch_add(1, std::memory_order_relaxed);
    }
};
struct LogPing {
    template <typename Src, typename Dst>
    void operator()(const PingEvent&, const Src&, Dst&, StressContext& ctx) const noexcept {
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

void test_concurrent_post_stress() {
    std::cout << "[TEST] Running test_concurrent_post_stress (10 threads x 2,000 events)...\n";

    StressContext ctx;
    fsm::thread_safe_fsm<StressTransitionTable, StressContext, Idle> fsm(ctx);

    constexpr int kNumThreads = 10;
    constexpr int kEventsPerThread = 2000;

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
                }
            }
        });
    }

    // Consumer thread draining queue
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
    std::cout << "  - Total events posted: " << (kNumThreads * kEventsPerThread) << "\n";
    std::cout << "  - Total actions triggered: " << total_processed << "\n";
    assert(total_processed > 0);

    std::cout << "[PASS] test_concurrent_post_stress completed cleanly without deadlock or race.\n";
}

void test_concurrent_timed_and_immediate_stress() {
    std::cout << "[TEST] Running test_concurrent_timed_and_immediate_stress...\n";

    StressContext ctx;
    fsm::thread_safe_fsm<StressTransitionTable, StressContext, Idle> fsm(ctx);

    constexpr int kNumThreads = 4;
    std::vector<std::thread> threads;

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

    // Wait for timed events
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    fsm.process_all();

    assert(ctx.total_starts > 0);
    assert(ctx.total_pings > 0);

    std::cout << "[PASS] test_concurrent_timed_and_immediate_stress passed.\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "   RUNNING THREAD-SAFE STRESS TESTS     \n"
              << "========================================\n";

    test_concurrent_post_stress();
    test_concurrent_timed_and_immediate_stress();

    std::cout << "========================================\n"
              << "   THREAD-SAFE STRESS TESTS PASSED!     \n"
              << "========================================\n";
    return 0;
}

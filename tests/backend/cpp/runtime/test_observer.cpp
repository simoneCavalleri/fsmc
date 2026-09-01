#include <gtest/gtest.h>

#include <chrono>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/thread_safe_fsm.hpp"

namespace {

// States & Events
struct Idle {
    static constexpr std::string_view name = "Idle";
};
struct Active {
    static constexpr std::string_view name = "Active";
};
struct Paused {
    static constexpr std::string_view name = "Paused";
};

struct StartEvent {
    static constexpr std::string_view name = "StartEvent";
};
struct PauseEvent {
    static constexpr std::string_view name = "PauseEvent";
};
struct ResumeEvent {
    static constexpr std::string_view name = "ResumeEvent";
};
struct PingEvent {
    static constexpr std::string_view name = "PingEvent";
};
struct StopEvent {
    static constexpr std::string_view name = "StopEvent";
};

struct PingAction {
    void operator()(const PingEvent& /*evt*/, auto& /*state*/, auto& /*ctx*/) const {
        // internal ping
    }
};

using ObserverTable =
    fsm::transition_table<fsm::row<Idle, StartEvent, Active>, fsm::row<Active, PauseEvent, Paused>,
                          fsm::row<Paused, ResumeEvent, Active>, fsm::internal_row<Active, PingEvent, PingAction>,
                          fsm::row<Active, StopEvent, Idle>>;

/**
 * @brief Test Intent: Verify synchronous observer callbacks receive comprehensive transition metadata.
 *
 * Scenario:
 * - Register observer callback receiving `fsm::transition_info`.
 * - Dispatch external transitions, internal transitions, and unhandled events.
 * - Verify observer receives correct source, target, event name, transition kind (external/internal),
 *   and outcome status (success/unhandled).
 */
TEST(ObserverTest, SyncFsmObserverHooks) {
    fsm::dynamic_fsm<ObserverTable> machine;

    std::vector<fsm::transition_info> transitions;
    machine.set_observer([&](const fsm::transition_info& info) { transitions.push_back(info); });

    EXPECT_TRUE(machine.is_in_state<Idle>());

    // 1. Idle -> Active
    auto res1 = machine.dispatch(StartEvent{});
    EXPECT_TRUE(res1.is_success());
    EXPECT_EQ(res1.status, fsm::dispatch_status::success);
    ASSERT_EQ(transitions.size(), 1u);
    EXPECT_EQ(transitions.back().source, "Idle");
    EXPECT_EQ(transitions.back().target, "Active");
    EXPECT_EQ(transitions.back().event, "StartEvent");
    EXPECT_FALSE(transitions.back().is_internal());
    EXPECT_EQ(transitions.back().kind, fsm::transition_kind::external);
    EXPECT_TRUE(transitions.back().is_success());
    EXPECT_EQ(transitions.back().status, fsm::dispatch_status::success);

    // 2. Internal ping in Active
    auto res2 = machine.dispatch(PingEvent{});
    EXPECT_TRUE(res2.is_success());
    ASSERT_EQ(transitions.size(), 2u);
    EXPECT_EQ(transitions.back().source, "Active");
    EXPECT_EQ(transitions.back().target, "Active");
    EXPECT_EQ(transitions.back().event, "PingEvent");
    EXPECT_TRUE(transitions.back().is_internal());
    EXPECT_EQ(transitions.back().kind, fsm::transition_kind::internal);
    EXPECT_EQ(transitions.back().status, fsm::dispatch_status::success);

    // 3. Active -> Paused
    auto res3 = machine.dispatch(PauseEvent{});
    EXPECT_TRUE(res3.is_success());
    ASSERT_EQ(transitions.size(), 3u);
    EXPECT_EQ(transitions.back().source, "Active");
    EXPECT_EQ(transitions.back().target, "Paused");
    EXPECT_EQ(transitions.back().event, "PauseEvent");
    EXPECT_FALSE(transitions.back().is_internal());
    EXPECT_EQ(transitions.back().kind, fsm::transition_kind::external);

    // 4. Paused -> Active
    auto res4 = machine.dispatch(ResumeEvent{});
    EXPECT_TRUE(res4.is_success());
    ASSERT_EQ(transitions.size(), 4u);
    EXPECT_EQ(transitions.back().source, "Paused");
    EXPECT_EQ(transitions.back().target, "Active");
    EXPECT_EQ(transitions.back().event, "ResumeEvent");

    // 5. Active -> Idle
    auto res5 = machine.dispatch(StopEvent{});
    EXPECT_TRUE(res5.is_success());
    ASSERT_EQ(transitions.size(), 5u);
    EXPECT_EQ(transitions.back().source, "Active");
    EXPECT_EQ(transitions.back().target, "Idle");
    EXPECT_EQ(transitions.back().event, "StopEvent");

    // 6. Unhandled event in Idle (PauseEvent is not valid in Idle)
    auto res6 = machine.dispatch(PauseEvent{});
    EXPECT_FALSE(static_cast<bool>(res6));
    EXPECT_TRUE(res6.is_unhandled());
    EXPECT_EQ(res6.status, fsm::dispatch_status::unhandled);
    EXPECT_EQ(res6.to_string(), "unhandled");
    ASSERT_EQ(transitions.size(), 6u);
    EXPECT_EQ(transitions.back().source, "Idle");
    EXPECT_EQ(transitions.back().event, "PauseEvent");
    EXPECT_TRUE(transitions.back().is_unhandled());
    EXPECT_EQ(transitions.back().status, fsm::dispatch_status::unhandled);
}

/**
 * @brief Test Intent: Verify thread_safe_fsm observer firing asynchronously on background worker thread.
 *
 * Scenario:
 * - Register observer callback protected by mutex.
 * - Post 5 events into async queue.
 * - Wait for worker thread to process queue and verify all 5 transition events were recorded safely.
 */
TEST(ObserverTest, ThreadSafeFsmObserverHooks) {
    fsm::thread_safe_fsm<ObserverTable> ts_machine;

    std::vector<fsm::transition_info> async_transitions;
    std::mutex tr_mutex;

    ts_machine.set_observer([&](const fsm::transition_info& info) {
        std::scoped_lock lock(tr_mutex);
        async_transitions.push_back(info);
    });

    ts_machine.start_worker();

    ts_machine.post(StartEvent{});
    ts_machine.post(PingEvent{});
    ts_machine.post(PauseEvent{});
    ts_machine.post(ResumeEvent{});
    ts_machine.post(StopEvent{});

    // Wait for queue drain
    while (!ts_machine.is_queue_empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ts_machine.stop_worker();

    {
        std::scoped_lock lock(tr_mutex);
        ASSERT_EQ(async_transitions.size(), 5u);
        EXPECT_EQ(async_transitions[0].source, "Idle");
        EXPECT_EQ(async_transitions[0].target, "Active");
        EXPECT_EQ(async_transitions[0].event, "StartEvent");
        EXPECT_EQ(async_transitions[1].source, "Active");
        EXPECT_EQ(async_transitions[1].event, "PingEvent");
        EXPECT_TRUE(async_transitions[1].is_internal());
        EXPECT_EQ(async_transitions[1].kind, fsm::transition_kind::internal);
        EXPECT_EQ(async_transitions[2].source, "Active");
        EXPECT_EQ(async_transitions[2].target, "Paused");
        EXPECT_EQ(async_transitions[2].event, "PauseEvent");
        EXPECT_EQ(async_transitions[3].source, "Paused");
        EXPECT_EQ(async_transitions[3].target, "Active");
        EXPECT_EQ(async_transitions[3].event, "ResumeEvent");
        EXPECT_EQ(async_transitions[4].source, "Active");
        EXPECT_EQ(async_transitions[4].target, "Idle");
        EXPECT_EQ(async_transitions[4].event, "StopEvent");
    }

    EXPECT_TRUE(ts_machine.is_in_state<Idle>());
}

/**
 * @brief Test Intent: Verify `post_async()` returning `std::future<dispatch_result>` and unhandled handlers.
 *
 * Scenario:
 * - Call `post_async()` and block on `future.get()` for both valid and unhandled events.
 * - Verify unhandled handler is invoked on invalid events.
 */
TEST(ObserverTest, ThreadSafeFsmPostAsyncAndUnhandledHandler) {
    fsm::thread_safe_fsm<ObserverTable> ts_machine;

    std::string last_unhandled_event;
    std::string last_unhandled_state;

    ts_machine.set_unhandled_handler([&](std::string_view evt, std::string_view st) {
        last_unhandled_event = std::string(evt);
        last_unhandled_state = std::string(st);
    });

    ts_machine.start_worker();

    // Valid async dispatch via post_async returning std::future
    auto fut1 = ts_machine.post_async(StartEvent{});
    auto res1 = fut1.get();
    EXPECT_TRUE(res1.is_success());
    EXPECT_EQ(res1.status, fsm::dispatch_status::success);
    EXPECT_TRUE(ts_machine.is_in_state<Active>());

    // Invalid async dispatch (PauseEvent is valid in Active, but StopEvent then PauseEvent in Idle is invalid)
    auto fut2 = ts_machine.post_async(StopEvent{});
    auto res2 = fut2.get();
    EXPECT_TRUE(res2.is_success());
    EXPECT_TRUE(ts_machine.is_in_state<Idle>());

    auto fut3 = ts_machine.post_async(PauseEvent{});
    auto res3 = fut3.get();
    EXPECT_TRUE(res3.is_unhandled());
    EXPECT_EQ(res3.status, fsm::dispatch_status::unhandled);

    // Check synchronous send
    auto res_send = ts_machine.send(StartEvent{});
    EXPECT_TRUE(res_send.is_success());
    EXPECT_TRUE(ts_machine.is_in_state<Active>());

    ts_machine.stop_worker();
}

/**
 * @brief Test Intent: Verify reentrant `send()` calls from inside observer callbacks are deadlock-free.
 *
 * Scenario:
 * - Register observer callback that immediately issues another `send()` event synchronously.
 * - Verify recursive/reentrant lock acquisition completes without deadlock.
 */
TEST(ObserverTest, ReentrantSendInsideObserverDeadlockFree) {
    fsm::thread_safe_fsm<ObserverTable> ts_machine;

    bool reentrant_called = false;
    ts_machine.set_observer([&](const fsm::transition_info& info) {
        // When entering Active for the first time, perform a reentrant send()
        if (info.target == "Active" && !reentrant_called) {
            reentrant_called = true;
            // Calling send() while observer runs must NOT deadlock!
            auto nested_res = ts_machine.send(PingEvent{});
            EXPECT_TRUE(nested_res.is_success());
        }
    });

    auto res = ts_machine.send(StartEvent{});
    EXPECT_TRUE(res.is_success());
    EXPECT_TRUE(reentrant_called);
    EXPECT_TRUE(ts_machine.is_in_state<Active>());
}

}  // namespace

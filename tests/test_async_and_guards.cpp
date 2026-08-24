#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <string>
#include <vector>

#include "fsm/fsm.hpp"
#include "fsm/thread_safe_fsm.hpp"
#include "fsm/type_traits.hpp"

namespace {

// ============================================================================
// State & Event Definitions for Guards and Deferred Testing
// ============================================================================

struct GuardContext {
    bool allow_transition = false;
    bool guard_executed = false;
    int guard_checks = 0;
};

struct StateInitial {
    static constexpr std::string_view name = "StateInitial";
};
struct StateGuarded {
    static constexpr std::string_view name = "StateGuarded";
};
struct StateDeferredTarget {
    static constexpr std::string_view name = "StateDeferredTarget";
};

struct EvAttempt {
    static constexpr std::string_view name = "EvAttempt";
};

struct EvDeferred {
    static constexpr std::string_view name = "EvDeferred";
};

struct EvUnlock {
    static constexpr std::string_view name = "EvUnlock";
};

struct CustomGuard {
    bool operator()(const EvAttempt& /*evt*/, const auto& /*src*/, GuardContext& ctx, auto& /*sm*/) const noexcept {
        ctx.guard_executed = true;
        ++ctx.guard_checks;
        return ctx.allow_transition;
    }
};

// StateInitial defers EvDeferred until transitioning to StateGuarded
struct StateInitialWithDeferred {
    using deferred_events = fsm::type_list<EvDeferred>;
};

using GuardTestTable =
    fsm::transition_table<fsm::transition<StateInitial, EvAttempt, StateGuarded, fsm::no_action, CustomGuard>,
                          fsm::transition<StateInitialWithDeferred, EvUnlock, StateGuarded>,
                          fsm::transition<StateGuarded, EvDeferred, StateDeferredTarget>>;

// ============================================================================
// 1. Automatic Tests for Guard Evaluation and Rejection
// ============================================================================

TEST(AsyncAndGuardsTest, GuardRejectionAndAcceptance) {
    GuardContext ctx;
    ctx.allow_transition = false;

    fsm::fsm<GuardTestTable, GuardContext, StateInitial> sm(ctx);
    EXPECT_TRUE(sm.is_in_state<StateInitial>());

    // 1. Guard rejects transition
    auto res_rejected = sm.dispatch(EvAttempt{});
    EXPECT_FALSE(static_cast<bool>(res_rejected));
    EXPECT_TRUE(res_rejected.is_guard_rejected());
    EXPECT_FALSE(res_rejected.is_success());
    EXPECT_EQ(res_rejected.status, fsm::dispatch_status::guard_rejected);
    EXPECT_EQ(res_rejected.to_string(), "guard_rejected");
    EXPECT_TRUE(ctx.guard_executed);
    EXPECT_EQ(ctx.guard_checks, 1);
    EXPECT_TRUE(sm.is_in_state<StateInitial>());

    // 2. Guard allows transition
    ctx.allow_transition = true;
    auto res_accepted = sm.dispatch(EvAttempt{});
    EXPECT_TRUE(static_cast<bool>(res_accepted));
    EXPECT_TRUE(res_accepted.is_success());
    EXPECT_EQ(res_accepted.status, fsm::dispatch_status::success);
    EXPECT_EQ(ctx.guard_checks, 2);
    EXPECT_TRUE(sm.is_in_state<StateGuarded>());
}

// ============================================================================
// 2. Automatic Tests for Deferred Events Queue and Replay
// ============================================================================

TEST(AsyncAndGuardsTest, DeferredEventsQueuingAndReplay) {
    GuardContext ctx;
    fsm::fsm<GuardTestTable, GuardContext, StateInitialWithDeferred> sm(ctx);
    EXPECT_TRUE(sm.is_in_state<StateInitialWithDeferred>());

    // EvDeferred is deferred in StateInitialWithDeferred
    auto res_def = sm.dispatch(EvDeferred{});
    EXPECT_TRUE(static_cast<bool>(res_def));
    EXPECT_TRUE(res_def.is_deferred());
    EXPECT_EQ(res_def.status, fsm::dispatch_status::deferred);
    EXPECT_EQ(res_def.to_string(), "deferred");
    EXPECT_EQ(sm.deferred_count(), 1u);
    EXPECT_TRUE(sm.is_in_state<StateInitialWithDeferred>());

    // Dispatching EvUnlock moves to StateGuarded, which accepts EvDeferred -> automatically replays and transitions!
    auto res_unlock = sm.dispatch(EvUnlock{});
    EXPECT_TRUE(res_unlock.is_success());
    EXPECT_EQ(sm.deferred_count(), 0u);
    EXPECT_TRUE(sm.is_in_state<StateDeferredTarget>());
}

// ============================================================================
// 3. Automatic Tests for ThreadSafeFsm post_async and Handlers
// ============================================================================

TEST(AsyncAndGuardsTest, ThreadSafeFsmPostAsyncAndHandlers) {
    GuardContext ctx;
    ctx.allow_transition = false;

    fsm::thread_safe_fsm<GuardTestTable, GuardContext, StateInitialWithDeferred> ts_sm(ctx);

    std::vector<std::string> rejected_events;
    std::vector<std::string> deferred_events;
    std::vector<std::string> failed_events;

    ts_sm.set_guard_rejected_handler(
        [&](std::string_view evt, std::string_view /*st*/) { rejected_events.emplace_back(evt); });

    ts_sm.set_deferred_handler(
        [&](std::string_view evt, std::string_view /*st*/) { deferred_events.emplace_back(evt); });

    ts_sm.set_dispatch_failure_handler([&](std::string_view evt, std::string_view /*st*/, fsm::dispatch_status /*s*/) {
        failed_events.emplace_back(evt);
    });

    ts_sm.start_worker();

    // 1. Post async for deferred event
    auto fut_deferred = ts_sm.post_async(EvDeferred{});
    ASSERT_EQ(fut_deferred.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    auto res_def = fut_deferred.get();
    EXPECT_TRUE(res_def.is_deferred());

    // 2. Post async for unlock event -> causes state change and re-dispatch of deferred event
    auto fut_unlock = ts_sm.post_async(EvUnlock{});
    ASSERT_EQ(fut_unlock.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    auto res_unlock = fut_unlock.get();
    EXPECT_TRUE(res_unlock.is_success());

    // FSM should now be in StateDeferredTarget
    while (!ts_sm.is_in_state<StateDeferredTarget>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(ts_sm.is_in_state<StateDeferredTarget>());

    // 3. Post with completion callback
    std::promise<fsm::dispatch_result> cb_promise;
    auto cb_future = cb_promise.get_future();
    ts_sm.post(EvAttempt{}, [&](fsm::dispatch_result r) { cb_promise.set_value(r); });

    ASSERT_EQ(cb_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    auto res_cb = cb_future.get();
    // In StateDeferredTarget, EvAttempt is unhandled
    EXPECT_TRUE(res_cb.is_unhandled());

    ts_sm.stop_worker();

    // Handlers validation
    ASSERT_EQ(deferred_events.size(), 1u);
    EXPECT_EQ(deferred_events[0], "EvDeferred");
    ASSERT_EQ(failed_events.size(), 1u);
    EXPECT_EQ(failed_events[0], "EvAttempt");
}

// ============================================================================
// 4. Exception Safety in Worker & post_async
// ============================================================================

struct ThrowingAction {
    void operator()(const auto& /*evt*/, auto& /*src*/, auto& /*dst*/, auto& /*ctx*/) const {
        throw std::runtime_error("Simulated hardware failure");
    }
};

struct StateFault {
    static constexpr std::string_view name = "StateFault";
};
struct EvFault {
    static constexpr std::string_view name = "EvFault";
};

using ExceptionTestTable = fsm::transition_table<fsm::transition<StateInitial, EvFault, StateFault, ThrowingAction>,
                                                 fsm::transition<StateInitial, EvUnlock, StateGuarded>>;

TEST(AsyncAndGuardsTest, WorkerExceptionSafetyAndFuturePropagation) {
    GuardContext ctx;
    fsm::thread_safe_fsm<ExceptionTestTable, GuardContext, StateInitial> ts_sm(ctx);

    ts_sm.start_worker();

    // 1. Dispatching event whose action throws
    auto fut = ts_sm.post_async(EvFault{});
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_THROW({ fut.get(); }, std::runtime_error);

    // 2. The worker thread must STILL be alive and functional after the exception!
    auto fut2 = ts_sm.post_async(EvUnlock{});
    ASSERT_EQ(fut2.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    auto res2 = fut2.get();
    EXPECT_TRUE(res2.is_success());
    EXPECT_TRUE(ts_sm.is_in_state<StateGuarded>());

    ts_sm.stop_worker();
}

// ============================================================================
// 5. Enqueue in Manual Mode & Auto-Starting post_async
// ============================================================================

TEST(AsyncAndGuardsTest, ManualEnqueueAndAutoStartPostAsync) {
    GuardContext ctx;
    fsm::thread_safe_fsm<ExceptionTestTable, GuardContext, StateInitial> ts_sm(ctx);

    // 1. Manual mode: enqueue() queues without starting worker
    ts_sm.enqueue(EvUnlock{});
    EXPECT_FALSE(ts_sm.is_worker_running());
    EXPECT_EQ(ts_sm.pending_events(), 1u);
    EXPECT_EQ(ts_sm.process_all(), 1u);
    EXPECT_TRUE(ts_sm.is_in_state<StateGuarded>());

    // 2. post_async automatically starts worker so fut.get() never hangs
    auto fut = ts_sm.post_async(EvFault{});
    EXPECT_TRUE(ts_sm.is_worker_running());
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    auto res = fut.get();
    EXPECT_TRUE(res.is_unhandled());

    // 3. with_context provides synchronized, thread-safe Context access
    ts_sm.with_context([](GuardContext& c) {
        c.allow_transition = true;
        c.guard_checks = 42;
    });
    EXPECT_EQ(ts_sm.with_context([](const GuardContext& c) { return c.guard_checks; }), 42);

    ts_sm.stop_worker();
}

// ============================================================================
// 6. Transition Info with Strongly-Typed transition_kind
// ============================================================================

TEST(AsyncAndGuardsTest, TransitionInfoExplicitKind) {
    fsm::transition_info ext_info{"Idle", "Active", "Start", fsm::dispatch_status::success,
                                  fsm::transition_kind::external};
    EXPECT_TRUE(ext_info.is_external());
    EXPECT_FALSE(ext_info.is_internal());
    EXPECT_EQ(ext_info.kind, fsm::transition_kind::external);
    EXPECT_EQ(fsm::to_string(ext_info.kind), "external");

    fsm::transition_info int_info{"Active", "Active", "Ping", fsm::dispatch_status::success,
                                  fsm::transition_kind::internal};
    EXPECT_TRUE(int_info.is_internal());
    EXPECT_FALSE(int_info.is_external());
    EXPECT_EQ(int_info.kind, fsm::transition_kind::internal);
    EXPECT_EQ(fsm::to_string(int_info.kind), "internal");
}

// ============================================================================
// 7. Exception Handler Registration, Error Capture, and Last Exception Query
// ============================================================================

TEST(AsyncAndGuardsTest, ExceptionHandlerRegistrationAndLastException) {
    GuardContext ctx;
    fsm::thread_safe_fsm<ExceptionTestTable, GuardContext, StateInitial> ts_sm(ctx);

    std::string caught_message;
    ts_sm.set_exception_handler([&](const std::exception_ptr& ex) {
        try {
            if (ex)
                std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            caught_message = e.what();
        }
    });

    ts_sm.start_worker();

    // Trigger throwing action via post (fire and forget)
    ts_sm.post(EvFault{});

    // Wait until exception is captured
    for (int i = 0; i < 50 && caught_message.empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_EQ(caught_message, "Simulated hardware failure");
    EXPECT_NE(ts_sm.last_exception(), nullptr);

    ts_sm.clear_last_exception();
    EXPECT_EQ(ts_sm.last_exception(), nullptr);

    ts_sm.stop_worker();
}

// ============================================================================
// 8. Observers and Handlers Invoked Outside Lock (Deadlock-Free State Queries)
// ============================================================================

TEST(AsyncAndGuardsTest, ObserverInvokedOutsideLockCanQueryState) {
    GuardContext ctx;
    fsm::thread_safe_fsm<ExceptionTestTable, GuardContext, StateInitial> ts_sm(ctx);

    std::vector<std::string> observed_states;
    ts_sm.set_observer([&](const fsm::transition_info& info) {
        // Querying current_state_name() from within observer callback:
        // Must NOT deadlock even if called concurrently!
        observed_states.push_back(ts_sm.current_state_name());
        EXPECT_EQ(info.target, "StateGuarded");
    });

    auto res = ts_sm.send(EvUnlock{});
    EXPECT_TRUE(res.is_success());
    ASSERT_EQ(observed_states.size(), 1u);
    EXPECT_EQ(observed_states[0], "StateGuarded");
}

// ============================================================================
// 9. Self-Stop from Worker Callback (No Self-Join Deadlock)
// ============================================================================

TEST(AsyncAndGuardsTest, SelfStopWorkerFromWorkerThreadDoesNotDeadlock) {
    GuardContext ctx;
    fsm::thread_safe_fsm<ExceptionTestTable, GuardContext, StateInitial> ts_sm(ctx);

    std::atomic<bool> stop_invoked{false};
    ts_sm.set_observer([&](const fsm::transition_info& info) {
        if (info.target == "StateGuarded") {
            // Calling stop_worker() directly from within the worker thread's observer execution!
            ts_sm.stop_worker();
            stop_invoked.store(true);
        }
    });

    ts_sm.start_worker();
    ts_sm.post(EvUnlock{});

    // Wait with timeout to ensure no deadlock occurred
    for (int i = 0; i < 50 && !stop_invoked.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(stop_invoked.load());
    EXPECT_FALSE(ts_sm.is_worker_running());
    EXPECT_TRUE(ts_sm.is_in_state<StateGuarded>());
}

// ============================================================================
// 10. Cascading Events During Shutdown Are Fully Drained
// ============================================================================

struct StateA {
    static constexpr std::string_view name = "StateA";
};
struct StateB {
    static constexpr std::string_view name = "StateB";
};
struct StateC {
    static constexpr std::string_view name = "StateC";
};

struct EvToB {
    static constexpr std::string_view name = "EvToB";
};
struct EvToC {
    static constexpr std::string_view name = "EvToC";
};

using CascadeTable =
    fsm::transition_table<fsm::transition<StateA, EvToB, StateB>, fsm::transition<StateB, EvToC, StateC>>;

TEST(AsyncAndGuardsTest, CascadingEventsDuringShutdownDrained) {
    // Test A: With running worker shutting down
    {
        fsm::thread_safe_fsm<CascadeTable, fsm::no_context, StateA> ts_sm;
        ts_sm.set_observer([&](const fsm::transition_info& info) {
            if (info.target == "StateB") {
                ts_sm.post(EvToC{});
            }
        });

        ts_sm.start_worker();
        ts_sm.post(EvToB{});
        ts_sm.stop_worker();  // Must wait and drain EvToB AND cascading EvToC

        EXPECT_TRUE(ts_sm.is_in_state<StateC>());
        EXPECT_TRUE(ts_sm.is_queue_empty());
    }

    // Test B: In manual polling mode with process_all
    {
        fsm::thread_safe_fsm<CascadeTable, fsm::no_context, StateA> ts_sm;
        ts_sm.set_observer([&](const fsm::transition_info& info) {
            if (info.target == "StateB") {
                ts_sm.enqueue(EvToC{});
            }
        });

        ts_sm.enqueue(EvToB{});
        ts_sm.process_all();  // Must drain EvToB AND cascading EvToC

        EXPECT_TRUE(ts_sm.is_in_state<StateC>());
        EXPECT_TRUE(ts_sm.is_queue_empty());
    }
}

// ============================================================================
// 11. Destructor Drains All Queued Tasks Safely Before Destruction
// ============================================================================

TEST(AsyncAndGuardsTest, DestructorDrainsAllQueuedTasksSafely) {
    int events_processed = 0;
    {
        fsm::thread_safe_fsm<CascadeTable, fsm::no_context, StateA> ts_sm;
        ts_sm.set_observer([&](const fsm::transition_info&) { ++events_processed; });

        // Enqueue without starting worker
        ts_sm.enqueue(EvToB{});
        EXPECT_EQ(ts_sm.pending_events(), 1u);
        // Destructor ~thread_safe_fsm() will run here, calling stop_worker() and process_all()
    }
    // Verifies all tasks were executed cleanly before destruction
    EXPECT_EQ(events_processed, 1);
}

}  // namespace

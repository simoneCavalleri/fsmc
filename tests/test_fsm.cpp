#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "fsm/fsm.hpp"
#include "fsm/thread_safe_fsm.hpp"

namespace {

// ============================================================================
// Test 1: Basic Transitions & State Inspection
// ============================================================================

struct StateIdle {};
struct StateRunning {};
struct StateStopped {};

struct StartEvent {};
struct StopEvent {};
struct ResetEvent {};

using SimpleTable = fsm::transition_table<fsm::transition<StateIdle, StartEvent, StateRunning>,
                                          fsm::transition<StateRunning, StopEvent, StateStopped>,
                                          fsm::transition<StateStopped, ResetEvent, StateIdle>>;

TEST(FsmCoreTest, BasicTransitionsAndIntrospection) {
    fsm::fsm<SimpleTable> state_machine;

    static_assert(decltype(state_machine)::state_count == 3);
    static_assert(decltype(state_machine)::transition_count == 3);
    static_assert(decltype(state_machine)::has_state<StateIdle>);
    static_assert(decltype(state_machine)::has_state<StateRunning>);
    static_assert(decltype(state_machine)::has_event<StartEvent>);
    static_assert(!decltype(state_machine)::has_state<int>);

    EXPECT_TRUE(state_machine.is_in_state<StateIdle>());
    EXPECT_FALSE(state_machine.is_in_state<StateRunning>());

    // Dispatch StartEvent
    auto is_handled = state_machine.dispatch(StartEvent{});
    EXPECT_TRUE(is_handled.is_success());
    EXPECT_TRUE(state_machine.is_in_state<StateRunning>());

    // Invalid transition (ResetEvent from Running)
    is_handled = state_machine.dispatch(ResetEvent{});
    EXPECT_TRUE(is_handled.is_unhandled());
    EXPECT_TRUE(state_machine.is_in_state<StateRunning>());

    // Dispatch StopEvent
    is_handled = state_machine.dispatch(StopEvent{});
    EXPECT_TRUE(is_handled.is_success());
    EXPECT_TRUE(state_machine.is_in_state<StateStopped>());

    // Dispatch ResetEvent
    is_handled = state_machine.dispatch(ResetEvent{});
    EXPECT_TRUE(is_handled.is_success());
    EXPECT_TRUE(state_machine.is_in_state<StateIdle>());
}

// ============================================================================
// Test 2: on_enter & on_exit hooks execution order and payloads
// ============================================================================

struct HookTracker {
    static std::vector<std::string>& log() {
        static std::vector<std::string> instance;
        return instance;
    }
    static void clear() { log().clear(); }
    static void add(const std::string& msg) { log().emplace_back(msg); }
};

struct StateA {
    static void on_enter() { HookTracker::add("StateA::on_enter"); }
    static void on_exit() { HookTracker::add("StateA::on_exit"); }
};

struct EventGotoB {
    std::string message;
};

struct StateB {
    static void on_enter(const EventGotoB& evt) { HookTracker::add("StateB::on_enter with payload: " + evt.message); }
    static void on_exit(const EventGotoB& /*evt*/) { HookTracker::add("StateB::on_exit with EventGotoB"); }
    static void on_exit() { HookTracker::add("StateB::on_exit void fallback"); }
};

struct EventGotoA {};

struct CustomAction {
    void operator()(const EventGotoB& evt, StateA& /*src*/, StateB& /*dst*/) const {
        HookTracker::add("Action(EventGotoB: " + evt.message + ")");
    }
};

using HookTable = fsm::transition_table<fsm::transition<StateA, EventGotoB, StateB, CustomAction>,
                                        fsm::transition<StateB, EventGotoA, StateA>>;

TEST(FsmCoreTest, HooksExecutionOrderAndPayloads) {
    HookTracker::clear();

    // Machine creation -> StateA on_enter should be called
    fsm::fsm<HookTable> state_machine;

    EXPECT_TRUE(state_machine.is_in_state<StateA>());
    ASSERT_EQ(HookTracker::log().size(), 1u);
    EXPECT_EQ(HookTracker::log()[0], "StateA::on_enter");

    // Transition to B with payload
    HookTracker::clear();
    state_machine.dispatch(EventGotoB{"Hello FSM"});

    EXPECT_TRUE(state_machine.is_in_state<StateB>());
    ASSERT_EQ(HookTracker::log().size(), 3u);
    EXPECT_EQ(HookTracker::log()[0], "StateA::on_exit");
    EXPECT_EQ(HookTracker::log()[1], "Action(EventGotoB: Hello FSM)");
    EXPECT_EQ(HookTracker::log()[2], "StateB::on_enter with payload: Hello FSM");
}

// ============================================================================
// Test 3: Guards validation
// ============================================================================

struct StateLocked {};
struct StateUnlocked {};

struct UnlockEvent {
    int key = 0;
};

struct IsValidKeyGuard {
    [[nodiscard]] bool operator()(const UnlockEvent& evt, const StateLocked& /*state*/) const noexcept {
        return evt.key == 42;
    }
};

using GuardTable =
    fsm::transition_table<fsm::transition<StateLocked, UnlockEvent, StateUnlocked, fsm::no_action, IsValidKeyGuard>>;

TEST(FsmCoreTest, GuardValidation) {
    fsm::fsm<GuardTable> state_machine;

    // Wrong key -> guard rejects
    fsm::dispatch_result res_fail = state_machine.dispatch(UnlockEvent{10});
    EXPECT_FALSE(res_fail);
    EXPECT_TRUE(res_fail.is_guard_rejected());
    EXPECT_EQ(res_fail.status, fsm::dispatch_status::guard_rejected);
    EXPECT_EQ(res_fail.to_string(), "guard_rejected");
    EXPECT_TRUE(state_machine.is_in_state<StateLocked>());

    // Unhandled event
    fsm::dispatch_result res_unhandled = state_machine.dispatch(ResetEvent{});
    EXPECT_FALSE(res_unhandled);
    EXPECT_TRUE(res_unhandled.is_unhandled());
    EXPECT_EQ(res_unhandled.status, fsm::dispatch_status::unhandled);
    EXPECT_EQ(res_unhandled.to_string(), "unhandled");

    // Correct key -> guard accepts
    fsm::dispatch_result res_ok = state_machine.dispatch(UnlockEvent{42});
    EXPECT_TRUE(res_ok);
    EXPECT_TRUE(res_ok.is_success());
    EXPECT_EQ(res_ok.status, fsm::dispatch_status::success);
    EXPECT_EQ(res_ok.to_string(), "success");
    EXPECT_TRUE(state_machine.is_in_state<StateUnlocked>());
}

// ============================================================================
// Test 4: ThreadSafe wrapper & event queue
// ============================================================================

struct CounterState {
    int count = 0;
};

struct IncrementEvent {
    int amount = 1;
};

struct DecrementEvent {
    int amount = 1;
};

struct IncAction {
    void operator()(const IncrementEvent& evt, const CounterState& src, CounterState& dst) const noexcept {
        dst.count = src.count + evt.amount;
    }
};

struct DecAction {
    void operator()(const DecrementEvent& evt, const CounterState& src, CounterState& dst) const noexcept {
        dst.count = src.count - evt.amount;
    }
};

using CounterTable = fsm::transition_table<fsm::transition<CounterState, IncrementEvent, CounterState, IncAction>,
                                           fsm::transition<CounterState, DecrementEvent, CounterState, DecAction>>;

TEST(FsmCoreTest, ThreadSafeQueueManualProcessing) {
    fsm::thread_safe_fsm<CounterTable> ts_machine;

    // Synchronous send
    ts_machine.send(IncrementEvent{10});
    EXPECT_EQ(ts_machine.with_state([](const CounterState& state) { return state.count; }), 10);

    // Asynchronous queue enqueue (Manual Polling Mode)
    ts_machine.enqueue(IncrementEvent{5});
    ts_machine.enqueue(DecrementEvent{3});
    EXPECT_EQ(ts_machine.pending_events(), 2u);

    // Process all queued events
    const std::size_t processed_count = ts_machine.process_all();
    EXPECT_EQ(processed_count, 2u);
    EXPECT_TRUE(ts_machine.is_queue_empty());
    EXPECT_EQ(ts_machine.with_state([](const CounterState& state) { return state.count; }), 12);
}

TEST(FsmCoreTest, ConcurrentMultithreadedWorker) {
    fsm::thread_safe_fsm<CounterTable> ts_machine;
    ts_machine.start_worker();

    constexpr int total_threads = 10;
    constexpr int increments_per_thread = 100;
    std::vector<std::thread> producers;
    producers.reserve(total_threads);

    for (int idx = 0; idx < total_threads; ++idx) {
        producers.emplace_back([&ts_machine]() {
            for (int step = 0; step < increments_per_thread; ++step) {
                ts_machine.post(IncrementEvent{1});
            }
        });
    }

    for (auto& producer_thread : producers) {
        producer_thread.join();
    }

    // Wait for all queued items to be processed
    while (!ts_machine.is_queue_empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Stop worker thread cleanly
    ts_machine.stop_worker();

    const int final_count = ts_machine.with_state([](const CounterState& state) { return state.count; });
    EXPECT_EQ(final_count, total_threads * increments_per_thread);
}

}  // namespace

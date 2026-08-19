#include <cassert>
#include <chrono>
#include <iostream>
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

void test_basic_transitions() {
    std::cout << "[TEST] Running test_basic_transitions...\n";

    fsm::fsm<SimpleTable> state_machine;

    static_assert(decltype(state_machine)::state_count == 3);
    static_assert(decltype(state_machine)::transition_count == 3);
    static_assert(decltype(state_machine)::has_state<StateIdle>);
    static_assert(decltype(state_machine)::has_state<StateRunning>);
    static_assert(decltype(state_machine)::has_event<StartEvent>);
    static_assert(!decltype(state_machine)::has_state<int>);

    assert(state_machine.is_in_state<StateIdle>());
    assert(!state_machine.is_in_state<StateRunning>());

    // Dispatch StartEvent
    bool is_handled = state_machine.dispatch(StartEvent{});
    assert(is_handled);
    assert(state_machine.is_in_state<StateRunning>());

    // Invalid transition (ResetEvent from Running)
    is_handled = state_machine.dispatch(ResetEvent{});
    assert(!is_handled);
    assert(state_machine.is_in_state<StateRunning>());

    // Dispatch StopEvent
    is_handled = state_machine.dispatch(StopEvent{});
    assert(is_handled);
    assert(state_machine.is_in_state<StateStopped>());

    // Dispatch ResetEvent
    is_handled = state_machine.dispatch(ResetEvent{});
    assert(is_handled);
    assert(state_machine.is_in_state<StateIdle>());

    std::cout << "[PASS] test_basic_transitions passed.\n";
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

void test_hooks_order() {
    std::cout << "[TEST] Running test_hooks_order...\n";
    HookTracker::clear();

    // Machine creation -> StateA on_enter should be called
    fsm::fsm<HookTable> state_machine;

    assert(state_machine.is_in_state<StateA>());
    assert(HookTracker::log().size() == 1);
    assert(HookTracker::log()[0] == "StateA::on_enter");

    // Transition to B with payload
    HookTracker::clear();
    state_machine.dispatch(EventGotoB{"Hello FSM"});

    assert(state_machine.is_in_state<StateB>());
    assert(HookTracker::log().size() == 3);
    assert(HookTracker::log()[0] == "StateA::on_exit");
    assert(HookTracker::log()[1] == "Action(EventGotoB: Hello FSM)");
    assert(HookTracker::log()[2] == "StateB::on_enter with payload: Hello FSM");

    std::cout << "[PASS] test_hooks_order passed.\n";
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

void test_guards() {
    std::cout << "[TEST] Running test_guards...\n";

    fsm::fsm<GuardTable> state_machine;

    // Wrong key -> guard rejects
    bool is_handled = state_machine.dispatch(UnlockEvent{10});
    assert(!is_handled);
    assert(state_machine.is_in_state<StateLocked>());

    // Correct key -> guard accepts
    is_handled = state_machine.dispatch(UnlockEvent{42});
    assert(is_handled);
    assert(state_machine.is_in_state<StateUnlocked>());

    std::cout << "[PASS] test_guards passed.\n";
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

void test_thread_safe_queue() {
    std::cout << "[TEST] Running test_thread_safe_queue (manual processing)...\n";

    fsm::thread_safe_fsm<CounterTable> ts_machine;

    // Synchronous send
    ts_machine.send(IncrementEvent{10});
    assert(ts_machine.with_state([](const CounterState& state) { return state.count; }) == 10);

    // Asynchronous queue post
    ts_machine.post(IncrementEvent{5});
    ts_machine.post(DecrementEvent{3});
    assert(ts_machine.pending_events() == 2);

    // Process all queued events
    const std::size_t processed_count = ts_machine.process_all();
    assert(processed_count == 2);
    assert(ts_machine.is_queue_empty());
    assert(ts_machine.with_state([](const CounterState& state) { return state.count; }) == 12);

    std::cout << "[PASS] test_thread_safe_queue passed.\n";
}

void test_concurrent_multithreaded_worker() {
    std::cout << "[TEST] Running test_concurrent_multithreaded_worker...\n";

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
    assert(final_count == total_threads * increments_per_thread);

    std::cout << "[PASS] test_concurrent_multithreaded_worker passed. Final count = " << final_count << "\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "     RUNNING FSM UNIT TEST SUITE        \n"
              << "========================================\n";

    test_basic_transitions();
    test_hooks_order();
    test_guards();
    test_thread_safe_queue();
    test_concurrent_multithreaded_worker();

    std::cout << "========================================\n"
              << "     ALL FSM TESTS PASSED (5/5)!        \n"
              << "========================================\n";
    return 0;
}

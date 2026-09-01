#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/thread_safe_fsm.hpp"

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

/**
 * @brief Test Intent: Verify basic synchronous state transitions and compile-time introspection.
 *
 * Scenario:
 * - Define a 3-state machine (Idle -> Running -> Stopped -> Idle).
 * - Verify compile-time type introspection (state_count, transition_count, has_state, has_event).
 * - Dispatch valid events in sequence and verify immediate active state updates.
 * - Dispatch unhandled events and verify that the machine remains in the current state with an unhandled result.
 */
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
    ASSERT_TRUE(is_handled.trace.has_value());
    EXPECT_EQ(is_handled.trace->source, "StateIdle");
    EXPECT_EQ(is_handled.trace->target, "StateRunning");
    EXPECT_EQ(is_handled.trace->event, "StartEvent");

    // Invalid transition (ResetEvent from Running)
    is_handled = state_machine.dispatch(ResetEvent{});
    EXPECT_TRUE(is_handled.is_unhandled());
    EXPECT_TRUE(state_machine.is_in_state<StateRunning>());
    ASSERT_TRUE(is_handled.trace.has_value());
    EXPECT_EQ(is_handled.trace->source, "StateRunning");

    // Dispatch StopEvent
    is_handled = state_machine.dispatch(StopEvent{});
    EXPECT_TRUE(is_handled.is_success());
    EXPECT_TRUE(state_machine.is_in_state<StateStopped>());
    ASSERT_TRUE(is_handled.trace.has_value());
    EXPECT_EQ(is_handled.trace->source, "StateRunning");
    EXPECT_EQ(is_handled.trace->target, "StateStopped");

    // Dispatch ResetEvent
    is_handled = state_machine.dispatch(ResetEvent{});
    EXPECT_TRUE(is_handled.is_success());
    EXPECT_TRUE(state_machine.is_in_state<StateIdle>());
    ASSERT_TRUE(is_handled.trace.has_value());
    EXPECT_EQ(is_handled.trace->source, "StateStopped");
    EXPECT_EQ(is_handled.trace->target, "StateIdle");
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

/**
 * @brief Test Intent: Verify strict lifecycle hook execution order and event payload forwarding.
 *
 * Scenario:
 * - When entering initial state StateA: StateA::on_enter() must be called.
 * - When transitioning StateA -> StateB with EventGotoB{"Hello FSM"}:
 *   1. StateA::on_exit() is invoked.
 *   2. CustomAction is executed with the payload.
 *   3. StateB::on_enter(evt) is invoked with payload parameter.
 */
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

/**
 * @brief Test Intent: Verify guard predicate rejection, acceptance, and dispatch result statuses.
 *
 * Scenario:
 * - With key != 42: guard returns false, transition is rejected, state remains Locked, status is guard_rejected.
 * - With an unhandled event: status is unhandled, state remains Locked.
 * - With key == 42: guard returns true, transition succeeds, state becomes Unlocked, status is success.
 */
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

/**
 * @brief Test Intent: Verify thread_safe_fsm synchronous sending and manual batch processing.
 *
 * Scenario:
 * - Call send() synchronously to apply transition immediately under mutex.
 * - Call enqueue() to push events into thread-safe queue.
 * - Call process_all() to drain and execute queued events deterministically.
 */
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

/**
 * @brief Test Intent: Verify asynchronous background worker thread handling concurrent event posting.
 *
 * Scenario:
 * - Start worker thread with start_worker().
 * - Launch 10 concurrent producer threads, each posting 100 IncrementEvent events.
 * - Wait for worker thread to process all 1000 events.
 * - Verify final accumulated state count is exactly 1000 with zero race conditions.
 */
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

// ============================================================================
// Dual-Channel Machine for v0.4.0 Mixed Paradigm Verification
// ============================================================================

struct CmdBoost {
    static constexpr std::string_view name = "CmdBoost";
    double boost_val{0.0};
    constexpr CmdBoost() = default;
    constexpr explicit CmdBoost(double v) : boost_val(v) {}
};

struct CmdEmergency {
    static constexpr std::string_view name = "CmdEmergency";
};

struct IdleState {
    static constexpr std::string_view name = "Idle";
};

struct RunningState {
    static constexpr std::string_view name = "Running";
};

struct FaultState {
    static constexpr std::string_view name = "Fault";
};

struct MachineInPorts {
    double sensor_val{0.0};
};

struct MachineOutPorts {
    double actuator_cmd{0.0};
};

struct MachineRegisters {
    int retry_count{0};
};

struct MockMachineServices {
    bool alert_triggered{false};
    void ExternalAlert() { alert_triggered = true; }
};

struct OnBoostGuard {
    [[nodiscard]] constexpr bool operator()(const CmdBoost& cmd, const MachineInPorts& in,
                                            const MachineRegisters& /*reg*/) const noexcept {
        return in.sensor_val > 10.0 && cmd.boost_val <= 50.0;
    }
};

struct OnSensorDropGuard {
    [[nodiscard]] constexpr bool operator()(const MachineInPorts& in) const noexcept {
        return in.sensor_val < 5.0;
    }
};

struct OnBoostAction {
    void operator()(const CmdBoost& cmd, MachineOutPorts& out, MachineRegisters& reg) const noexcept {
        out.actuator_cmd = cmd.boost_val * 2.0;
        reg.retry_count = 0;
    }
};

struct OnSensorDropAction {
    void operator()(MachineOutPorts& out, MachineRegisters& /*reg*/) const noexcept {
        out.actuator_cmd = 0.0;
    }
};

struct ExternalAlertAction {
    template <typename Services>
    auto operator()(Services& srv) const -> decltype(srv.ExternalAlert()) {
        srv.ExternalAlert();
    }
};

using DualChannelTable = fsm::transition_table<
    fsm::row<IdleState, CmdBoost, RunningState>::when<OnBoostGuard>::then<OnBoostAction>,
    fsm::row<RunningState, fsm::anonymous_event, IdleState>::when<OnSensorDropGuard>::then<OnSensorDropAction>,
    fsm::row<RunningState, CmdEmergency, FaultState>::then<ExternalAlertAction>>;

using DualChannelFSM =
    fsm::fsm<DualChannelTable, MachineInPorts, MachineOutPorts, MachineRegisters, MockMachineServices, IdleState>;

/**
 * @brief Test Intent: Verify dual-mode execution (continuous sampled step + event-driven reactive dispatch) and zero-heap non-polymorphism.
 */
TEST(FsmCoreTest, DualChannelMachineDualParadigmAndZeroHeap) {
    // 1. Zero-Heap & No-Virtual Static Assertions
    static_assert(!std::is_polymorphic_v<DualChannelFSM>, "FSM class must not contain virtual vtables");
    static_assert(!std::is_polymorphic_v<IdleState>, "State structs must not be polymorphic");
    static_assert(!std::is_polymorphic_v<RunningState>, "State structs must not be polymorphic");
    static_assert(!std::is_polymorphic_v<FaultState>, "State structs must not be polymorphic");
    static_assert(!std::is_polymorphic_v<DualChannelTable>, "Transition table must be a pure compile-time type");

    // 2. Dual-Paradigm Execution
    MachineInPorts in{100.0};
    MachineOutPorts out{0.0};
    MachineRegisters reg{5};
    MockMachineServices srv;

    DualChannelFSM fsm(reg, srv);
    EXPECT_TRUE(fsm.is_in<IdleState>());

    // Sampled step in Idle -> remains in Idle
    in.sensor_val = 20.0;
    (void)fsm.step(in, out, srv);
    EXPECT_TRUE(fsm.is_in<IdleState>());

    // Reactive dispatch: CmdBoost
    auto disp_res = fsm.dispatch(CmdBoost{30.0}, in, out, srv);
    EXPECT_TRUE(disp_res.is_success());
    EXPECT_TRUE(fsm.is_in<RunningState>());
    EXPECT_DOUBLE_EQ(out.actuator_cmd, 60.0);
    EXPECT_EQ(fsm.registers().retry_count, 0);

    // Continuous sampled drop: sensor_val < 5.0 -> step() transitions back to Idle
    in.sensor_val = 2.5;
    auto step_drop = fsm.step(in, out, srv);
    EXPECT_TRUE(step_drop.is_success());
    EXPECT_TRUE(fsm.is_in<IdleState>());
    EXPECT_DOUBLE_EQ(out.actuator_cmd, 0.0);

    // Re-enter Running and trigger reactive emergency alert
    in.sensor_val = 25.0;
    fsm.dispatch(CmdBoost{45.0}, in, out, srv);
    EXPECT_TRUE(fsm.is_in<RunningState>());
    EXPECT_DOUBLE_EQ(out.actuator_cmd, 90.0);

    fsm.dispatch(CmdEmergency{}, in, out, srv);
    EXPECT_TRUE(fsm.is_in<FaultState>());
    EXPECT_TRUE(srv.alert_triggered);
}

}  // namespace

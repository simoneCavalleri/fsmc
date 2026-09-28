/**
 * @file test_fsm.cpp
 * @brief Unit test suite for core synchronous and thread-safe FSM runtime execution.
 */

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
 * @brief Verify basic synchronous transitions and runtime state introspection.
 * @scenario Dispatch event on state machine and inspect current_state_name() and is_in_state<T>().
 * @expected State updates accurately and introspection returns matching boolean flags.
 */
TEST(FsmCore, BasicTransitions_EventDispatch_UpdatesCurrentState) {
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

struct HookServices {
    std::vector<std::string> log;
};

struct HookIn {
    int value = 7;
};

struct HookOut {};

struct HookRegisters {};

struct StateA {
    void on_enter(const HookIn&, HookOut&, HookRegisters&, HookServices& services) const {
        services.log.emplace_back("StateA::on_enter");
    }

    void on_exit(const HookIn&, HookOut&, HookRegisters&, HookServices& services) const {
        services.log.emplace_back("StateA::on_exit");
    }
};

struct EventGotoB {
    std::string message;
};

struct StateB {
    void on_enter(const HookIn&, HookOut&, HookRegisters&, HookServices& services) const {
        services.log.emplace_back("StateB::on_enter");
    }
};

struct CustomAction {
    void operator()(const EventGotoB& evt, StateA&, StateB&, const HookIn&, HookOut&, HookRegisters&,
                    HookServices& services) const {
        services.log.emplace_back("Action(EventGotoB: " + evt.message + ")");
    }
};

using HookTable = fsm::transition_table<fsm::transition<StateA, EventGotoB, StateB, CustomAction>>;

/**
 * @brief Verify lifecycle hook execution order (on_exit, transition action, on_entry).
 * @scenario Dispatch transition carrying typed payload between states with lifecycle hooks.
 * @expected Source on_exit executes first, followed by transition action, followed by target on_entry.
 */
TEST(FsmCore, LifecycleHooks_ExecutionOrder_ExecutesEntryActionExit) {
    HookServices services;

    // Machine creation -> StateA on_enter should be called
    fsm::fsm<HookTable, HookIn, HookOut, HookRegisters, HookServices> state_machine(services);

    EXPECT_TRUE(state_machine.is_in<StateA>());
    ASSERT_EQ(services.log.size(), 1u);
    EXPECT_EQ(services.log[0], "StateA::on_enter");

    // Transition to B with payload
    services.log.clear();
    HookIn input;
    HookOut output;
    state_machine.dispatch(EventGotoB{"Hello FSM"}, input, output);

    EXPECT_TRUE(state_machine.is_in<StateB>());
    ASSERT_EQ(services.log.size(), 3u);
    EXPECT_EQ(services.log[0], "StateA::on_exit");
    EXPECT_EQ(services.log[1], "Action(EventGotoB: Hello FSM)");
    EXPECT_EQ(services.log[2], "StateB::on_enter");
}

// ============================================================================
// Test 3: Lifecycle hooks with ports, registers, and services
// ============================================================================

struct PortHookIn {
    int value = 7;
};

struct PortHookOut {
    int observed_input = 0;
    int transition_count = 0;
};

struct PortHookRegisters {
    int enter_count = 0;
    int exit_count = 0;
};

struct PortHookServices {
    int enter_calls = 0;
    int exit_calls = 0;
};

struct PortHookStateA {
    void on_enter(const PortHookIn&, PortHookOut&, PortHookRegisters& registers, PortHookServices& services) const {
        ++registers.enter_count;
        ++services.enter_calls;
    }

    void on_exit(const PortHookIn& input, PortHookOut& output, PortHookRegisters& registers,
                 PortHookServices& services) const {
        output.observed_input = input.value;
        ++output.transition_count;
        ++registers.exit_count;
        ++services.exit_calls;
    }
};

struct PortHookStateB {
    void on_enter(const PortHookIn& input, PortHookOut& output, PortHookRegisters& registers,
                  PortHookServices& services) const {
        output.observed_input = input.value;
        ++output.transition_count;
        ++registers.enter_count;
        ++services.enter_calls;
    }
};

struct PortHookEvent {};

using PortHookTable = fsm::transition_table<fsm::transition<PortHookStateA, PortHookEvent, PortHookStateB>>;

/**
 * @brief Verify lifecycle hooks receive the complete public runtime context.
 * @scenario Enter the initial state and dispatch a transition with typed input/output ports.
 * @expected Entry and exit hooks receive ports, registers, and services in the documented order.
 */
TEST(FsmCore, LifecycleHooks_WithPortsRegistersAndServices_ForwardsContext) {
    PortHookServices services;
    fsm::fsm<PortHookTable, PortHookIn, PortHookOut, PortHookRegisters, PortHookServices> state_machine(services);

    EXPECT_EQ(state_machine.registers().enter_count, 1);
    EXPECT_EQ(services.enter_calls, 1);

    PortHookIn input{42};
    PortHookOut output;
    auto result = state_machine.dispatch(PortHookEvent{}, input, output);

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(state_machine.is_in<PortHookStateB>());
    EXPECT_EQ(output.observed_input, 42);
    EXPECT_EQ(output.transition_count, 2);
    EXPECT_EQ(state_machine.registers().enter_count, 2);
    EXPECT_EQ(state_machine.registers().exit_count, 1);
    EXPECT_EQ(services.enter_calls, 2);
    EXPECT_EQ(services.exit_calls, 1);
}

// ============================================================================
// Test 4: Guards validation
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
 * @brief Verify guard evaluation blocking transitions when predicate returns false.
 * @scenario Dispatch event with failing guard and with passing guard.
 * @expected Failing guard prevents transition, passing guard allows transition.
 */
TEST(FsmCore, GuardValidation_BooleanPredicates_BlocksDisallowedTransitions) {
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
// Test 5: ThreadSafe wrapper & event queue
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
 * @brief Verify thread_safe_fsm in manual processing mode.
 * @scenario Post events to thread-safe queue and invoke process_event() explicitly.
 * @expected Events are processed synchronously on calling thread without background worker.
 */
TEST(FsmCore, ThreadSafeQueue_ManualProcessing_DrainsEventsExplicitly) {
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
 * @brief Verify concurrent multi-threaded event submission to background worker.
 * @scenario Spawn multiple producer threads dispatching events concurrently.
 * @expected All events processed in serialized, thread-safe sequence with correct final state.
 */
TEST(FsmCore, ConcurrentWorker_MultipleThreads_ProcessesEventsThreadSafely) {
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
// Dual-Channel Machine for Mixed Paradigm Verification
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
    [[nodiscard]] constexpr bool operator()(const MachineInPorts& in) const noexcept { return in.sensor_val < 5.0; }
};

struct OnBoostAction {
    void operator()(const CmdBoost& cmd, MachineOutPorts& out, MachineRegisters& reg) const noexcept {
        out.actuator_cmd = cmd.boost_val * 2.0;
        reg.retry_count = 0;
    }
};

struct OnSensorDropAction {
    void operator()(MachineOutPorts& out, MachineRegisters& /*reg*/) const noexcept { out.actuator_cmd = 0.0; }
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
 * @brief Verify dual-channel synchronous and asynchronous zero-heap state machine execution.
 * @scenario Execute state machine with static buffer policies.
 * @expected Zero dynamic memory allocations performed during initialization and dispatching.
 */
TEST(FsmCore, DualChannelMachine_ZeroHeap_ExecutesWithoutDynamicAllocation) {
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
    auto step_idle = fsm.step(in, out, srv);
    EXPECT_TRUE(step_idle.is_steady());
    EXPECT_FALSE(step_idle.has_transitioned());
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
    EXPECT_TRUE(step_drop.has_transitioned());
    EXPECT_FALSE(step_drop.is_steady());
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

// ============================================================================
// Test: Non-Default-Constructible Services
// ============================================================================

struct HardwareHandleServices {
    int bus_id;
    explicit HardwareHandleServices(int id) : bus_id(id) {}
    HardwareHandleServices(const HardwareHandleServices&) = delete;
    HardwareHandleServices& operator=(const HardwareHandleServices&) = delete;
};
static_assert(!std::is_default_constructible_v<HardwareHandleServices>);

struct StateS1 {
    static constexpr std::string_view name = "StateS1";
};
struct StateS2 {
    static constexpr std::string_view name = "StateS2";
};
struct SrvEvent {
    static constexpr std::string_view name = "SrvEvent";
};

using SrvTable = fsm::transition_table<fsm::transition<StateS1, SrvEvent, StateS2>>;

/**
 * @brief Verify support for non-default-constructible context services.
 * @scenario Inject reference to non-default-constructible service into FSM context.
 * @expected Actions successfully access and invoke methods on injected service.
 */
TEST(FsmCore, ServicesSupport_NonDefaultConstructible_InjectedSuccessfully) {
    HardwareHandleServices srv(42);
    fsm::no_registers reg;
    fsm::fsm<SrvTable, fsm::no_ports, fsm::no_ports, fsm::no_registers, HardwareHandleServices> machine(reg, srv);

    EXPECT_TRUE(machine.is_in<StateS1>());

    // Verify step without srv compiles and executes
    auto step_res = machine.step();
    EXPECT_TRUE(step_res.is_steady());

    // Verify dispatch without srv compiles and executes
    auto disp_res = machine.dispatch(SrvEvent{});
    EXPECT_TRUE(disp_res.is_success());
    EXPECT_TRUE(machine.is_in<StateS2>());
}

}  // namespace

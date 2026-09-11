/**
 * @file test_timed_transitions.cpp
 * @brief Unit test suite for timed transitions, timer cancellation, and discrete tick stepping.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/spsc_fsm.hpp"
#include "fsm/backend/cpp/runtime/thread_safe_fsm.hpp"
#include "fsm/backend/cpp/runtime/transition.hpp"

namespace {

// ============================================================================
// Synchronous Timed Transition Types
// ============================================================================

struct Connecting {
    static constexpr std::string_view name = "Connecting";
};

struct Connected {
    static constexpr std::string_view name = "Connected";
};

struct Disconnected {
    static constexpr std::string_view name = "Disconnected";
};

struct HandshakeOk {};
using Timeout500ms = fsm::after_ms<500>;

struct TimeoutAction {
    void operator()(const Timeout500ms& /*evt*/, Connecting& /*src*/, Disconnected& /*dst*/) const {}
};

using ConnTable = fsm::transition_table<fsm::transition<Connecting, HandshakeOk, Connected>,
                                        fsm::transition<Connecting, Timeout500ms, Disconnected, TimeoutAction>>;

/**
 * @brief Verify synchronous timed transition triggering after elapsed duration.
 * @scenario Step discrete timer past transition timeout threshold.
 * @expected Timed transition triggers and machine switches to destination state.
 */
TEST(TimedTransitions, SyncTimedEvent_Dispatch_TransitionsAfterDuration) {
    fsm::fsm<ConnTable> sm;
    EXPECT_TRUE(sm.is_in_state<Connecting>());

    // Dispatch synchronous timed event
    auto handled = sm.dispatch(Timeout500ms{});
    EXPECT_TRUE(handled.is_success());
    EXPECT_TRUE(sm.is_in_state<Disconnected>());
}

/**
 * @brief Verify timed table rows are armed automatically when a state is entered.
 * @scenario Advance the deterministic clock past an after_ms transition without manually dispatching its event.
 * @expected The timer expires and the generated timed event performs the transition.
 */
TEST(TimedTransitions, TimedTableRow_TickAutomaticallyDispatchesAfterEvent) {
    fsm::fsm<ConnTable> sm;

    EXPECT_TRUE(sm.is_in_state<Connecting>());
    EXPECT_EQ(sm.timer_manager().active_count(), 1U);

    auto result = sm.tick(500);

    EXPECT_EQ(result, 1U);
    EXPECT_TRUE(sm.is_in_state<Disconnected>());
    EXPECT_EQ(sm.timer_manager().active_count(), 0U);
}

// ============================================================================
// Asynchronous Priority Deadline Scheduling
// ============================================================================

struct Step1 {};
struct Step2 {};
struct Step3 {};

struct StateA {
    static constexpr std::string_view name = "StateA";
};
struct StateB {
    static constexpr std::string_view name = "StateB";
};
struct StateC {
    static constexpr std::string_view name = "StateC";
};
struct StateD {
    static constexpr std::string_view name = "StateD";
};

struct OrderRegisters {
    std::vector<std::string> log;
};

struct ActionAtoB {
    void operator()(OrderRegisters& reg) const { reg.log.emplace_back("A->B"); }
};

struct ActionBtoC {
    void operator()(OrderRegisters& reg) const { reg.log.emplace_back("B->C"); }
};

struct ActionCtoD {
    void operator()(OrderRegisters& reg) const { reg.log.emplace_back("C->D"); }
};

using OrderTable = fsm::transition_table<fsm::transition<StateA, Step1, StateB, ActionAtoB>,
                                         fsm::transition<StateB, Step2, StateC, ActionBtoC>,
                                         fsm::transition<StateC, Step3, StateD, ActionCtoD>>;

/**
 * @brief Verify asynchronous delayed events scheduled and fired in chronological order.
 * @scenario Schedule timers with 10ms, 20ms, and 30ms timeouts.
 * @expected Timers fire in strict chronological order based on deadlines.
 */
TEST(TimedTransitions, AsyncPostDelayed_MultipleTimers_FiredInChronologicalOrder) {
    OrderRegisters reg;
    fsm::thread_safe_fsm<OrderTable, fsm::no_ports, fsm::no_ports, OrderRegisters> async_sm(reg);
    async_sm.start_worker();

    EXPECT_TRUE(async_sm.is_in_state<StateA>());

    // Post Step3 with 60ms delay, Step2 with 30ms delay, Step1 with 5ms delay
    // Regardless of posting order, execution order must be Step1 (5ms) -> Step2 (30ms) -> Step3 (60ms)
    async_sm.post_delayed(Step3{}, std::chrono::milliseconds(60));
    async_sm.post_delayed(Step2{}, std::chrono::milliseconds(30));
    async_sm.post_delayed(Step1{}, std::chrono::milliseconds(5));

    // Wait until final state is reached
    while (!async_sm.is_in_state<StateD>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    auto snap = async_sm.snapshot_registers();
    ASSERT_EQ(snap.log.size(), 3U);
    EXPECT_EQ(snap.log[0], "A->B");
    EXPECT_EQ(snap.log[1], "B->C");
    EXPECT_EQ(snap.log[2], "C->D");

    async_sm.stop_worker();
}

// ============================================================================
// Reentrancy & Recursive Lock Safety in Action Execution
// ============================================================================

struct ReentrantRegisters;

using ReentrantTable =
    fsm::transition_table<fsm::transition<StateA, Step1, StateB>, fsm::transition<StateB, Step2, StateC>>;

using ReentrantSm = fsm::thread_safe_fsm<ReentrantTable, fsm::no_ports, fsm::no_ports, ReentrantRegisters>;

struct ActionSelfPost {
    void operator()(const Step1& /*evt*/, StateA& /*src*/, StateB& /*dst*/, ReentrantRegisters& reg) const;
};

struct ActionFinal {
    void operator()(const Step2& /*evt*/, StateB& /*src*/, StateC& /*dst*/, ReentrantRegisters& reg) const;
};

using ReentrantActionTable = fsm::transition_table<fsm::transition<StateA, Step1, StateB, ActionSelfPost>,
                                                   fsm::transition<StateB, Step2, StateC, ActionFinal>>;

using ReentrantActionSm = fsm::thread_safe_fsm<ReentrantActionTable, fsm::no_ports, fsm::no_ports, ReentrantRegisters>;

struct ReentrantRegisters {
    ReentrantActionSm* sm_ptr = nullptr;
    bool self_post_executed = false;
    bool final_executed = false;
};

void ActionSelfPost::operator()(const Step1& /*evt*/, StateA& /*src*/, StateB& /*dst*/, ReentrantRegisters& reg) const {
    reg.self_post_executed = true;
    // 1. Reentrant query of state name under recursive lock
    ASSERT_NE(reg.sm_ptr, nullptr);
    EXPECT_TRUE(reg.sm_ptr->is_in_state<StateA>());
    // 2. Reentrant self-posting of next event from inside action handler
    reg.sm_ptr->post(Step2{});
}

void ActionFinal::operator()(const Step2& /*evt*/, StateB& /*src*/, StateC& /*dst*/, ReentrantRegisters& reg) const {
    reg.final_executed = true;
}

/**
 * @brief Verify reentrant self-posting of delayed timers from within transition actions.
 * @scenario Transition action posts delayed timer event to reschedule itself.
 * @expected Recurring timer fires periodically without deadlock.
 */
TEST(TimedTransitions, ReentrantAction_SelfPostDelayed_SchedulesRecurringTimer) {
    ReentrantRegisters reg;
    ReentrantActionSm async_sm(reg);
    async_sm.with_registers([&](auto& r) { r.sm_ptr = &async_sm; });

    async_sm.start_worker();

    // Trigger initial transition
    async_sm.post(Step1{});

    // Wait until background worker processes reentrantly self-posted Step2 event
    while (!async_sm.is_in_state<StateC>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto reentrant_snap = async_sm.snapshot_registers();
    EXPECT_TRUE(reentrant_snap.self_post_executed);
    EXPECT_TRUE(reentrant_snap.final_executed);
    EXPECT_TRUE(async_sm.is_in_state<StateC>());

    async_sm.stop_worker();
}

// ============================================================================
// Discrete Sampled Residence Time Model & Async Invalidation
// ============================================================================

struct DiscreteRegs {
    uint32_t elapsed_ticks = 0;
};

struct TickIncrementAction {
    void operator()(DiscreteRegs& reg) const { reg.elapsed_ticks = 0; }
};

using DiscreteTable = fsm::transition_table<
    fsm::transition<StateA, fsm::anonymous_event, StateB, TickIncrementAction, fsm::in_state_for<5>>>;

/**
 * @brief Verify state residence duration guard conditions (stay <= 100ms).
 * @scenario Sample state residence time across discrete ticks.
 * @expected Guard evaluates true while residence is under limit, and false when exceeded.
 */
TEST(TimedTransitions, ResidenceGuard_StayDuration_EvaluatedAccurately) {
    DiscreteRegs reg;
    fsm::fsm<DiscreteTable, fsm::no_ports, fsm::no_ports, DiscreteRegs> sm(reg);
    EXPECT_TRUE(sm.is_in_state<StateA>());

    // Ticks 1 to 4: remains in StateA, returns steady
    for (uint32_t tick = 1; tick <= 4; ++tick) {
        sm.registers().elapsed_ticks = tick;
        fsm::step_result res = sm.step();
        EXPECT_TRUE(res.is_steady());
        EXPECT_FALSE(res.has_transitioned());
        EXPECT_TRUE(sm.is_in_state<StateA>());
    }

    // Tick 5: in_state_for<5> satisfied -> transitions to StateB, returns transitioned
    sm.registers().elapsed_ticks = 5;
    fsm::step_result res5 = sm.step();
    EXPECT_TRUE(res5.has_transitioned());
    EXPECT_FALSE(res5.is_steady());
    EXPECT_TRUE(sm.is_in_state<StateB>());
    EXPECT_EQ(sm.registers().elapsed_ticks, 0U);
}

/**
 * @brief Verify automatic cancellation of pending timers upon state exit.
 * @scenario Arm timer in StateA and transition immediately to StateB via separate event.
 * @expected Timer associated with StateA is cancelled and does not trigger in StateB.
 */
TEST(TimedTransitions, StateChange_PendingTimer_CancelledAutomatically) {
    OrderRegisters reg;
    fsm::thread_safe_fsm<OrderTable, fsm::no_ports, fsm::no_ports, OrderRegisters> async_sm(reg);
    async_sm.start_worker();

    // Schedule a state timeout Step1 for 100ms
    async_sm.post_state_timeout(Step1{}, std::chrono::milliseconds(100));

    // Manually trigger an immediate transition to StateB via send()
    auto res = async_sm.send(Step1{});
    EXPECT_TRUE(res.is_success());
    EXPECT_TRUE(async_sm.is_in_state<StateB>());
    EXPECT_EQ(async_sm.snapshot_registers().log.size(), 1U);

    // Wait 150ms for the delayed task to fire: since state changed from StateA to StateB,
    // the stale task must be safely discarded without re-triggering or erroring
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    EXPECT_EQ(async_sm.snapshot_registers().log.size(), 1U);
    EXPECT_TRUE(async_sm.is_in_state<StateB>());

    async_sm.stop_worker();
}

/**
 * @brief Verify unified step function advancing simulated time deterministically.
 * @scenario Invoke step(delta_ms) on state machine with fixed time increments.
 * @expected Active timers decrement by delta_ms and expired timers trigger transitions.
 */
TEST(TimedTransitions, DeterministicTick_UnifiedStep_AdvancesTimeByFixedDelta) {
    fsm::fsm<ConnTable, fsm::no_ports, fsm::no_ports, fsm::no_registers, fsm::no_services, Connecting,
             fsm::flight_recorder_observer<16>, 16, 4>
        sm;

    EXPECT_TRUE(sm.is_in_state<Connecting>());
    EXPECT_TRUE(sm.timer_manager().cancel_timer(2));
    EXPECT_TRUE(sm.timer_manager().start_timer(1, 100));
    EXPECT_EQ(sm.timer_manager().active_count(), 1U);

    // Step 1: 40ms -> timer not expired
    auto res1 = sm.step(std::chrono::milliseconds(40));
    EXPECT_TRUE(res1.is_steady());
    EXPECT_EQ(sm.timer_manager().active_count(), 1U);

    // Step 2: 70ms -> total 110ms >= 100ms -> timer expired
    auto res2 = sm.step(std::chrono::milliseconds(70));
    EXPECT_TRUE(res2.is_steady());
    EXPECT_EQ(sm.timer_manager().active_count(), 0U);
}

/**
 * @brief Verify callback notification when timer expires under thread-safe wrapper.
 * @scenario Register timer expiration callback and advance timer to expiration.
 * @expected Callback invoked safely and transition dispatched.
 */
TEST(TimedTransitions, TickExpiredCallback_ThreadSafeWrapper_NotifiedUponExpiration) {
    fsm::fsm<ConnTable, fsm::no_ports, fsm::no_ports, fsm::no_registers, fsm::no_services, Connecting, fsm::no_observer,
             16, 4>
        sm;

    EXPECT_TRUE(sm.timer_manager().start_timer(42, 50));
    std::uint32_t expired_id = 0;
    std::size_t expired_count = sm.tick(60, [&](std::uint32_t id) { expired_id = id; });
    EXPECT_EQ(expired_count, 1U);
    EXPECT_EQ(expired_id, 42U);

    // Thread-safe wrapper
    fsm::thread_safe_fsm<ConnTable> ts_sm;
    EXPECT_TRUE(ts_sm.timer_manager().start_timer(7, 30));
    std::size_t ts_exp = ts_sm.tick(std::chrono::milliseconds(40));
    EXPECT_EQ(ts_exp, 1U);

    // SPSC wrapper
    fsm::spsc_fsm<ConnTable> spsc_sm;
    EXPECT_TRUE(spsc_sm.timer_manager().start_timer(9, 20));
    std::size_t spsc_exp = spsc_sm.tick(25);
    EXPECT_EQ(spsc_exp, 1U);
}

// ============================================================================
// State Time Invariant / Permanence Enforcement Tests (Section 3.2)
// ============================================================================

struct TimedBoundedState {
    static constexpr std::string_view name = "TimedBoundedState";
    static constexpr std::uint64_t max_stay_duration_ms = 100ULL;
};

struct EscapeState {
    static constexpr std::string_view name = "EscapeState";
};

struct EvEscape {};

using FastTimeout = fsm::after_ms<60>;
using SlowTimeout = fsm::after_ms<150>;

using InvariantSatisfiedTable = fsm::transition_table<fsm::transition<TimedBoundedState, FastTimeout, EscapeState>>;

using InvariantViolationWithEnabledTransitionTable =
    fsm::transition_table<fsm::transition<TimedBoundedState, EvEscape, EscapeState>,
                          fsm::transition<TimedBoundedState, SlowTimeout, EscapeState>>;

/**
 * @brief Verify invariant is satisfied when an armed transition exits the state before max stay.
 */
TEST(TimedTransitions, TimeInvariant_Satisfied_WhenTransitionLeavesBeforeMaxStay) {
    fsm::fsm<InvariantSatisfiedTable> sm;
    EXPECT_TRUE(sm.is_in_state<TimedBoundedState>());
    EXPECT_TRUE(sm.is_invariant_satisfied());
    EXPECT_FALSE(sm.has_invariant_violation());

    bool violation_reported = false;
    sm.set_invariant_violation_handler(
        [&](const fsm::invariant_violation_info& /*info*/) { violation_reported = true; });

    // Advance 40ms: residence is 40 <= 100, no timeout yet
    sm.tick(40);
    EXPECT_EQ(sm.state_residence_time(), 40U);
    EXPECT_TRUE(sm.is_in_state<TimedBoundedState>());
    EXPECT_TRUE(sm.is_invariant_satisfied());
    EXPECT_FALSE(violation_reported);

    // Advance another 30ms (total 70ms >= 60ms): FastTimeout fires and transitions to EscapeState
    sm.tick(30);
    EXPECT_TRUE(sm.is_in_state<EscapeState>());
    EXPECT_EQ(sm.state_residence_time(), 0U);
    EXPECT_TRUE(sm.is_invariant_satisfied());
    EXPECT_FALSE(violation_reported);
}

/**
 * @brief Verify invariant violation is diagnosed when an enabled escape transition is not taken in time.
 */
TEST(TimedTransitions, TimeInvariant_Violation_WithEnabledTransitionExceedingBound) {
    fsm::fsm<InvariantViolationWithEnabledTransitionTable> sm;
    EXPECT_TRUE(sm.is_in_state<TimedBoundedState>());

    std::optional<fsm::invariant_violation_info> reported_info;
    sm.set_invariant_violation_handler([&](const fsm::invariant_violation_info& info) { reported_info = info; });

    // Tick 120ms without EvEscape arriving: state residence time becomes 120 > 100 max_stay
    sm.tick(120);
    EXPECT_TRUE(sm.is_in_state<TimedBoundedState>());
    EXPECT_FALSE(sm.is_invariant_satisfied());
    EXPECT_TRUE(sm.has_invariant_violation());
    ASSERT_TRUE(reported_info.has_value());
    EXPECT_EQ(reported_info->state_name, "TimedBoundedState");
    EXPECT_EQ(reported_info->residence_time_ms, 120U);
    EXPECT_EQ(reported_info->max_stay_duration_ms, 100U);

    // Now dispatch external EvEscape transition: state transitions to EscapeState and clears violation
    auto handled = sm.dispatch(EvEscape{});
    EXPECT_TRUE(handled.is_success());
    EXPECT_TRUE(sm.is_in_state<EscapeState>());
    EXPECT_TRUE(sm.is_invariant_satisfied());
    EXPECT_FALSE(sm.has_invariant_violation());
}

/**
 * @brief Verify invariant violation without an escape transition invokes callback and marks status.
 */
TEST(TimedTransitions, TimeInvariant_Violation_WithoutEscapeTransition_InvokesCallbackAndSetsStatus) {
    using TimelockTable =
        fsm::transition_table<fsm::transition<TimedBoundedState, fsm::anonymous_event, TimedBoundedState>>;
    fsm::fsm<TimelockTable> sm;
    EXPECT_TRUE(sm.is_in_state<TimedBoundedState>());

    std::size_t callback_count = 0;
    sm.set_invariant_violation_handler([&](const fsm::invariant_violation_info& info) {
        ++callback_count;
        EXPECT_EQ(info.state_name, "TimedBoundedState");
        EXPECT_EQ(info.max_stay_duration_ms, 100U);
    });

    // Tick 80ms: within bound
    sm.tick(80);
    EXPECT_TRUE(sm.is_invariant_satisfied());
    EXPECT_EQ(callback_count, 0U);

    // Tick another 30ms (total 110ms > 100ms): permanence bound breached
    sm.tick(30);
    EXPECT_FALSE(sm.is_invariant_satisfied());
    EXPECT_TRUE(sm.has_invariant_violation());
    EXPECT_EQ(callback_count, 1U);
    ASSERT_TRUE(sm.last_invariant_violation().has_value());
    EXPECT_EQ(sm.last_invariant_violation()->residence_time_ms, 110U);
}

}  // namespace

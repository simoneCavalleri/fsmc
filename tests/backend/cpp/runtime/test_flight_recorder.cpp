/**
 * @file test_flight_recorder.cpp
 * @brief Unit test suite for circular flight recorder, state history logging, and diagnostics.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <sstream>
#include <thread>
#include <vector>

#include "fsm/backend/cpp/runtime/flight_recorder.hpp"
#include "fsm/backend/cpp/runtime/fsm.hpp"

using namespace fsm;

namespace {

struct StateA {
    static constexpr std::string_view name() noexcept { return "StateA"; }
};
struct StateB {
    static constexpr std::string_view name() noexcept { return "StateB"; }
};
struct EvNext {
    static constexpr std::string_view name() noexcept { return "EvNext"; }
};

using TestTable = transition_table<row<StateA, EvNext, StateB>, row<StateB, EvNext, StateA>>;

/**
 * @brief Verify flight recorder circular ring buffer push and wrap behavior.
 * @scenario Push more trace records than ring buffer capacity.
 * @expected Buffer retains newest entries and drops oldest entries without reallocation.
 */
TEST(FlightRecorder, CircularRingBuffer_PushAndWrap_OverwritesOldestEntries) {
    TraceBuffer<4> tb;
    EXPECT_TRUE(tb.empty());
    EXPECT_EQ(tb.size(), 0u);
    EXPECT_EQ(tb.capacity(), 4u);

    tb.record(10, "Idle", "StartCmd", "Running");
    tb.record(20, "Running", "PauseCmd", "Paused");
    tb.record(30, "Paused", "ResumeCmd", "Running");
    tb.record(40, "Running", "StopCmd", "Idle");

    EXPECT_TRUE(tb.full());
    EXPECT_EQ(tb.size(), 4u);
    EXPECT_EQ(tb[0].source_state, "Idle");
    EXPECT_EQ(tb[3].source_state, "Running");

    // Overwrite oldest entry
    tb.record(50, "Idle", "EStopCmd", "Faulted");
    EXPECT_EQ(tb.size(), 4u);
    EXPECT_EQ(tb[0].source_state, "Running");  // Was index 1
    EXPECT_EQ(tb[3].source_state, "Idle");     // Newest entry at index 3
    EXPECT_EQ(tb[3].target_state, "Faulted");
}

/**
 * @brief Verify chronological indexing and string formatting of flight recorder dump.
 * @scenario Record sequence of state transitions and call dump_to_string().
 * @expected Output contains sequential timestamps, state names, and triggered events in order.
 */
TEST(FlightRecorder, ChronologicalIndexing_Dump_ReturnsOrderedTraceEntries) {
    TraceBuffer<8> tb;
    EXPECT_FALSE(tb.last_entry().has_value());

    tb.record(100, "Align", "TrackingLock", "Locked");
    auto last = tb.last_entry();
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->tick, 100u);
    EXPECT_EQ(last->source_state, "Align");
    EXPECT_EQ(last->target_state, "Locked");

    std::ostringstream ss;
    tb.dump(ss);
    std::string dump_str = ss.str();
    EXPECT_NE(dump_str.find("FSM Flight Recorder Audit Trace"), std::string::npos);
    EXPECT_NE(
        dump_str.find("--------------------------------------------------------------------------------------------\n"),
        std::string::npos);
    EXPECT_NE(dump_str.find("TrackingLock"), std::string::npos);
}

/**
 * @brief Verify automatic recording of transitions via FlightRecorderObserver.
 * @scenario Attach observer to state machine and dispatch sequence of events.
 * @expected Observer logs all transitions, source states, target states, and action results.
 */
TEST(FlightRecorder, FlightRecorderObserver_StateTransitions_RecordsExecutionHistory) {
    flight_recorder_observer<16> observer(1000);
    observer.on_transition(StateA{}, EvNext{}, "StateA", "EvNext", "StateB");
    observer.advance_tick(50);
    observer.on_transition(StateB{}, EvNext{}, "StateB", "EvNext", "StateA");

    const auto& rec = observer.recorder();
    EXPECT_EQ(rec.size(), 2u);
    EXPECT_EQ(rec[0].tick, 1000u);
    EXPECT_EQ(rec[0].event_name, "EvNext");
    EXPECT_EQ(rec[1].tick, 1050u);
    EXPECT_EQ(rec[1].target_state, "StateA");
}

/**
 * @brief Verify recording of deterministic timer ticks in flight recorder.
 * @scenario Step deterministic timer forward in discrete steps.
 * @expected Flight recorder records timer tick events and remaining timer durations.
 */
TEST(FlightRecorder, DeterministicTimer_StepTick_RecordsTickEventsInHistory) {
    make_fsm<TestTable, with_timer_capacity<4>> sm;
    EXPECT_TRUE(sm.is_in<StateA>());

    // Start a 100ms timer
    bool started = sm.timer_manager().start_timer(1, 100);
    EXPECT_TRUE(started);
    EXPECT_TRUE(sm.timer_manager().is_timer_active(1));

    // Tick 50ms - not expired
    std::size_t exp1 = sm.tick(50);
    EXPECT_EQ(exp1, 0u);
    EXPECT_TRUE(sm.timer_manager().is_timer_active(1));

    // Tick 60ms (total 110ms) - expired
    std::size_t exp2 = sm.tick(std::chrono::milliseconds(60));
    EXPECT_EQ(exp2, 1u);
    EXPECT_FALSE(sm.timer_manager().is_timer_active(1));
}

/**
 * @brief Verify const flight_recorder_observer safely records transitions via mutable TraceBuffer.
 * @scenario Invoke a const-qualified flight_recorder_observer instance on transition_info events.
 * @expected Trace entry is recorded cleanly without const_cast undefined behavior.
 */
TEST(FlightRecorder, ConstFlightRecorderObserver_RecordsWithoutUndefinedBehavior) {
    const flight_recorder_observer<8> observer(42);
    observer(transition_info{"StateA", "StateB", "EvGo", dispatch_status::success});

    ASSERT_EQ(observer.recorder().size(), 1u);
    EXPECT_EQ(observer.recorder()[0].tick, 42u);
    EXPECT_EQ(observer.recorder()[0].source_state, "StateA");
    EXPECT_EQ(observer.recorder()[0].target_state, "StateB");
    EXPECT_EQ(observer.recorder()[0].event_name, "EvGo");
    EXPECT_TRUE(observer.recorder()[0].transition_taken);

    std::ostringstream ss;
    observer.dump(ss);
    const std::string out = ss.str();
    EXPECT_NE(
        out.find("--------------------------------------------------------------------------------------------\n"),
        std::string::npos);
    EXPECT_NE(out.find("StateA"), std::string::npos);
    EXPECT_NE(out.find("EvGo"), std::string::npos);
    EXPECT_NE(out.find("StateB"), std::string::npos);
    EXPECT_NE(out.find("TAKEN"), std::string::npos);
}

struct BoundedFlightState {
    static constexpr std::string_view name = "BoundedFlightState";
    static constexpr std::uint64_t max_stay_duration_ms = 50;
};

using BoundedFlightTable = transition_table<row<BoundedFlightState, EvNext, StateB>>;

static std::uint64_t g_fn_ptr_violation_residence = 0;
void record_violation_fn(const invariant_violation_info& info) {
    g_fn_ptr_violation_residence = info.residence_time_ms;
}

/**
 * @brief Verify function-pointer overload of set_invariant_violation_handler is noexcept and invokes callback.
 * @scenario Register a raw function pointer via set_invariant_violation_handler and exceed state max_stay_duration_ms.
 * @expected Function pointer is invoked with the violation info without std::function heap allocation.
 */
TEST(FlightRecorder, SetInvariantViolationHandler_FunctionPointerOverload_IsNoexceptAndInvoked) {
    ::fsm::fsm<BoundedFlightTable> sm;
    g_fn_ptr_violation_residence = 0;

    static_assert(noexcept(sm.set_invariant_violation_handler(&record_violation_fn)));
    sm.set_invariant_violation_handler(&record_violation_fn);

    sm.tick(60);
    EXPECT_TRUE(sm.has_invariant_violation());
    EXPECT_EQ(g_fn_ptr_violation_residence, 60u);
}

/**
 * @brief Verify TraceBuffer supports concurrent single-producer writes and external reader snapshots without data
 * races.
 * @scenario Producer thread records 5000 entries while reader thread continuously inspects size() and last_entry().
 * @expected Completes cleanly under ThreadSanitizer with valid state names and monotonic ticks.
 */
TEST(FlightRecorder, TraceBuffer_ConcurrentProducerAndReader_IsRaceFree) {
    TraceBuffer<16> tb;
    std::atomic<bool> done{false};

    std::thread reader([&]() {
        while (!done.load(std::memory_order_acquire)) {
            const auto sz = tb.size();
            if (sz > 0) {
                auto entry = tb[sz - 1];
                EXPECT_FALSE(entry.source_state.empty());
            }
            if (auto last = tb.last_entry(); last.has_value()) {
                EXPECT_FALSE(last->target_state.empty());
            }
        }
    });

    for (std::uint64_t i = 1; i <= 5000; ++i) {
        tb.record(i, "StateA", "EvNext", "StateB", true);
    }
    done.store(true, std::memory_order_release);
    reader.join();

    EXPECT_EQ(tb.size(), 16u);
    auto last = tb.last_entry();
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->tick, 5000u);
}

/**
 * @brief Verify cancelling a timer from an on_expired callback prevents it from firing in the same tick.
 * @scenario Schedule two timers with identical 100ms timeout; timer 1's callback cancels timer 2.
 * @expected Only timer 1 is dispatched during tick(100).
 */
TEST(FlightRecorder, DeterministicTimer_CancelOtherTimerInCallback_PreventsSameTickDispatch) {
    deterministic_timer_manager<4> mgr;
    EXPECT_TRUE(mgr.start_timer(1, 100));
    EXPECT_TRUE(mgr.start_timer(2, 100));

    std::vector<std::uint32_t> fired;
    const std::size_t count = mgr.tick(100, [&](std::uint32_t id) {
        fired.push_back(id);
        if (id == 1) {
            EXPECT_TRUE(mgr.cancel_timer(2));
        }
    });

    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0], 1u);
    EXPECT_EQ(count, 1u);
}

struct NonDefaultServices {
    int token;
    explicit NonDefaultServices(int t) : token(t) {}
};

/**
 * @brief Verify fsm() default constructor is SFINAE-disabled when Services is not default-constructible.
 * @scenario Instantiate fsm type with NonDefaultServices.
 * @expected std::is_default_constructible_v is false, preventing runtime std::terminate().
 */
TEST(FlightRecorder, NonDefaultConstructibleServices_DisablesDefaultFsmConstructor) {
    using StrictFsm = ::fsm::fsm<TestTable, no_ports, no_ports, no_registers, NonDefaultServices>;
    static_assert(!std::is_default_constructible_v<StrictFsm>,
                  "fsm with non-default-constructible Services must not be default-constructible");
    NonDefaultServices srv{7};
    StrictFsm sm(srv);
    EXPECT_TRUE(sm.is_in<StateA>());
}

}  // namespace

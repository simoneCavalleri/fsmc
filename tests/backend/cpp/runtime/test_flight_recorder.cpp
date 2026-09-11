/**
 * @file test_flight_recorder.cpp
 * @brief Unit test suite for circular flight recorder, state history logging, and diagnostics.
 */

#include <gtest/gtest.h>

#include <sstream>

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

}  // namespace

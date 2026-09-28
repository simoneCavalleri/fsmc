/**
 * @file test_timed_automata_verification.cpp
 * @brief Unit tests for timed automata deadlock pass, SMV clock emission, and deterministic runtime timer management.
 */

#include <gtest/gtest.h>

#include "fsm/backend/cpp/runtime/deterministic_timer.hpp"
#include "fsm/backend/formal/smv_serializer.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/trigger.hpp"
#include "fsm/middleend/passes/timed_deadlock_pass.hpp"

using namespace fsm;
using namespace fsm::diagnostic;
using namespace fsm::middleend;
using namespace fsm::middleend::passes;
using namespace fsm::backend;
using namespace fsm::backend::formal;
using namespace fsm::ir;

/**
 * @brief Verify TimedDeadlockPass flags racing timeouts without explicit priority differentiation.
 * @scenario State 'Active' has a 1000ms timed transition and an immediate 'EvCancel' transition, both with priority 0.
 * @expected Pass emits warning diagnostic W0403 identifying potential timeout racing hazard.
 */
TEST(TimedDeadlockPass, RacingTimeoutAndEventSamePriority_EmitsAmbiguityWarning) {
    FsmIr model;
    model.add_or_get_state("Active");
    model.add_or_get_state("TimeoutState");
    model.add_or_get_state("ErrorState");

    // Timed transition with 0 priority
    TransitionEdge t_timed;
    t_timed.source = "Active";
    t_timed.target = "TimeoutState";
    t_timed.priority = 0;
    t_timed.trigger = TimeTrigger(TimeTriggerKind::After, 1000, TimeUnit::Milliseconds);
    model.add_transition(t_timed);

    // Immediate event transition with 0 priority
    TransitionEdge t_event;
    t_event.source = "Active";
    t_event.target = "ErrorState";
    t_event.priority = 0;
    t_event.event = "EvCancel";
    model.add_transition(t_event);

    DiagnosticEngine diag;
    TimedDeadlockPass pass;
    pass.run(model, diag);

    EXPECT_TRUE(diag.has_warnings());
    bool found_w0403 = false;
    for (const auto& w : diag.get_diagnostics()) {
        if (w.code == "W0403") {
            found_w0403 = true;
            break;
        }
    }
    EXPECT_TRUE(found_w0403);
}

/**
 * @brief Verify SMV formal serializer emits discrete tick counter variables and next-state assignments for timers.
 * @scenario Model with 500ms timer transition and variable with physical unit 'm/s'.
 * @expected Serialized SMV module declares timer bounds, next(timer) transitions, and physical unit comments.
 */
TEST(SmvSerializer, TimeTriggeredTransitions_EmitsTickCountersAndNextTransitions) {
    FsmIr model;
    model.name = "TimedSystem";
    model.add_or_get_state("Standby");
    model.add_or_get_state("Operational");

    TransitionEdge t;
    t.source = "Standby";
    t.target = "Operational";
    t.trigger = TimeTrigger(TimeTriggerKind::After, 500, TimeUnit::Milliseconds);
    model.add_transition(t);

    VariableDefinition var("speed", DataType::float32(), "0");
    var.physical_unit = "m/s";
    var.min_value = 0;
    var.max_value = 100;
    model.add_variable(var);

    std::string smv_code = SmvSerializer::serialize(model);

    EXPECT_NE(smv_code.find("timer_Standby : 0..500;"), std::string::npos);
    EXPECT_NE(smv_code.find("init(timer_Standby) := 0;"), std::string::npos);
    EXPECT_NE(smv_code.find("next(timer_Standby) :="), std::string::npos);
    EXPECT_NE(smv_code.find("timer_Standby >= 500"), std::string::npos);
    EXPECT_NE(smv_code.find("physical unit: m/s"), std::string::npos);
}

/**
 * @brief Verify deterministic timer manager handles one-shot and periodic timer steps without drifting.
 * @scenario Timer 101 started as one-shot 50ms; timer 102 started as periodic 100ms; time advanced in discrete steps.
 * @expected Timers expire at exact discrete intervals, one-shot deactivates, and periodic timer remains active.
 */
TEST(DeterministicTimerManager, SingleShotAndPeriodicTimers_StepsAndExpiresDeterministically) {
    deterministic_timer_manager<4> mgr;
    EXPECT_TRUE(mgr.start_timer(101, 50, false));
    EXPECT_TRUE(mgr.start_timer(102, 100, true));  // periodic

    std::vector<uint32_t> expired;
    auto on_expired = [&](uint32_t id) {
        expired.push_back(id);
    };

    // Advance 30ms -> none expired
    EXPECT_EQ(mgr.tick(30, on_expired), 0);
    EXPECT_TRUE(expired.empty());

    // Advance another 30ms (total 60ms) -> timer 101 expired
    EXPECT_EQ(mgr.tick(30, on_expired), 1);
    ASSERT_EQ(expired.size(), 1);
    EXPECT_EQ(expired[0], 101);

    // Timer 101 was one-shot, so it is no longer active
    EXPECT_FALSE(mgr.is_timer_active(101));
    EXPECT_TRUE(mgr.is_timer_active(102));

    // Advance 50ms (total 110ms) -> timer 102 expired
    EXPECT_EQ(mgr.tick(50, on_expired), 1);
    ASSERT_EQ(expired.size(), 2);
    EXPECT_EQ(expired[1], 102);

    // Timer 102 was periodic, so it stays active
    EXPECT_TRUE(mgr.is_timer_active(102));
}

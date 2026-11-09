/**
 * @file test_formal_safety_passes.cpp
 * @brief Unit tests for Category D Formal Safety & Analysis Passes:
 *        LivelockAnalysisPass, PriorityConflictPass, TimedInvariantsVerifierPass, EventQueueBoundPass.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/event_queue_bound_pass.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"
#include "fsm/middleend/analysis/livelock_analysis_pass.hpp"
#include "fsm/middleend/analysis/priority_conflict_pass.hpp"
#include "fsm/middleend/analysis/timed_invariants_verifier_pass.hpp"

using namespace fsm;
using namespace fsm::ir;
using namespace fsm::middleend::analysis;
using namespace fsm::diagnostic;

// ============================================================================
// LivelockAnalysisPass Tests
// ============================================================================

/**
 * @brief Verify LivelockAnalysisPass validates systems without zero-time instantaneous cycles.
 * @scenario Cycle s1 -> s2 -> s1 where every transition is triggered by external discrete events ('EV1', 'EV2').
 * @expected Pass succeeds and no livelock errors are emitted.
 */
TEST(LivelockAnalysis, NonZeroTimeTransitions_PassesWithoutErrors) {
    FsmIr ir;
    ir.name = "HealthyModel";
    ir.initial_state_id = "s1";

    StateNode s1("s1", "s1");
    StateNode s2("s2", "s2");
    ir.states.push_back(s1);
    ir.states.push_back(s2);

    // Transitions with external event triggers are not zero-time
    TransitionEdge t1;
    t1.source_id = "s1";
    t1.target_id = "s2";
    t1.trigger = SignalTrigger("EV1");
    ir.add_transition(t1);

    TransitionEdge t2;
    t2.source_id = "s2";
    t2.target_id = "s1";
    t2.trigger = SignalTrigger("EV2");
    ir.add_transition(t2);

    DiagnosticEngine diag;
    LivelockAnalysisPass pass;
    EXPECT_TRUE(pass.run(ir, diag));
    EXPECT_FALSE(diag.has_errors());
}

/**
 * @brief Verify LivelockAnalysisPass detects infinite zero-time autonomous cycles.
 * @scenario Instantaneous loop s1 -> s2 -> s1 composed exclusively of anonymous trigger transitions.
 * @expected Pass returns false and logs diagnostic error E0401 indicating zero-time livelock.
 */
TEST(LivelockAnalysis, ZeroTimeAutonomousCycles_EmitsLivelockDiagnostic) {
    FsmIr ir;
    ir.name = "LivelockModel";
    ir.initial_state_id = "s1";

    StateNode s1("s1", "s1");
    StateNode s2("s2", "s2");
    ir.states.push_back(s1);
    ir.states.push_back(s2);

    // Instantaneous loop s1 -> s2 -> s1 with AnonymousTrigger
    TransitionEdge t1;
    t1.source_id = "s1";
    t1.target_id = "s2";
    t1.trigger = AnonymousTrigger{};
    ir.add_transition(t1);

    TransitionEdge t2;
    t2.source_id = "s2";
    t2.target_id = "s1";
    t2.trigger = AnonymousTrigger{};
    ir.add_transition(t2);

    DiagnosticEngine diag;
    LivelockAnalysisPass pass;
    EXPECT_FALSE(pass.run(ir, diag));
    EXPECT_TRUE(diag.has_errors());

    bool found_e0401 = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "E0401")
            found_e0401 = true;
    }
    EXPECT_TRUE(found_e0401);
}

/**
 * @brief Verify LivelockAnalysisPass and FsmValidator catch cycles with explicit "anonymous_event" /
 * "completion_event".
 * @scenario Cycle s1 -> s2 -> s1 where edges use string names "anonymous_event" and "completion_event".
 * @expected Both LivelockAnalysisPass and FsmValidator report livelock diagnostics.
 */
TEST(LivelockAnalysis, ExplicitAnonymousEventStringTransitions_DetectedAsLivelock) {
    FsmIr ir;
    ir.name = "ExplicitAnonLivelock";
    ir.initial_state = "s1";
    ir.initial_state_id = "s1";

    StateNode s1("s1", "s1");
    StateNode s2("s2", "s2");
    ir.states.push_back(s1);
    ir.states.push_back(s2);

    TransitionEdge t1;
    t1.source = "s1";
    t1.source_id = "s1";
    t1.target = "s2";
    t1.target_id = "s2";
    t1.event = "anonymous_event";
    ir.add_transition(t1);

    TransitionEdge t2;
    t2.source = "s2";
    t2.source_id = "s2";
    t2.target = "s1";
    t2.target_id = "s1";
    t2.event = "completion_event";
    ir.add_transition(t2);

    DiagnosticEngine diag;
    LivelockAnalysisPass pass;
    EXPECT_FALSE(pass.run(ir, diag));
    EXPECT_TRUE(diag.has_errors());

    bool found_e0401 = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "E0401")
            found_e0401 = true;
    }
    EXPECT_TRUE(found_e0401);

    const auto validation = FsmValidator::validate(ir);
    bool found_validator_livelock = false;
    for (const auto& d : validation.diagnostics) {
        if (d.category == "Livelock" && d.severity == DiagnosticSeverity::SafetyCritical) {
            found_validator_livelock = true;
            break;
        }
    }
    EXPECT_TRUE(found_validator_livelock);
}

/**
 * @brief Verify LivelockAnalysisPass detects cycles confined to Choice pseudostates in choice_nodes.
 */
TEST(LivelockAnalysis, ChoicePseudostatesZeroTimeLoop_DetectedAsLivelock) {
    FsmIr ir;
    ir.name = "ChoiceLivelockModel";
    ir.initial_state = "Idle";

    ir.add_state("Idle");
    ir.add_choice_node("ChoiceA");
    ir.add_choice_node("ChoiceB");

    // External event enters ChoiceA
    TransitionEdge t_enter;
    t_enter.source = "Idle";
    t_enter.target = "ChoiceA";
    t_enter.event = "EvTrigger";
    ir.add_transition(t_enter);

    // Zero-time instantaneous loop between ChoiceA and ChoiceB
    TransitionEdge t1;
    t1.source = "ChoiceA";
    t1.target = "ChoiceB";
    t1.trigger = AnonymousTrigger{};
    ir.add_transition(t1);

    TransitionEdge t2;
    t2.source = "ChoiceB";
    t2.target = "ChoiceA";
    t2.trigger = AnonymousTrigger{};
    ir.add_transition(t2);

    DiagnosticEngine diag;
    LivelockAnalysisPass pass;
    EXPECT_FALSE(pass.run(ir, diag));
    EXPECT_TRUE(diag.has_errors());

    bool found_e0401 = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "E0401")
            found_e0401 = true;
    }
    EXPECT_TRUE(found_e0401);
}

// ============================================================================
// PriorityConflictPass Tests
// ============================================================================

/**
 * @brief Verify PriorityConflictPass detects hierarchical policy preemption violations.
 * @scenario Composite parent with priority 2 and child with priority 1 competing on 'RESET' under OuterFirst policy.
 * @expected Pass detects preemption inversion and reports diagnostic error E0402.
 */
TEST(PriorityConflict, InvertedChildPrecedenceUnderOuterFirst_EmitsPriorityConflictDiagnostic) {
    FsmIr ir;
    ir.name = "PriorityConflictModel";
    ir.initial_state_id = "parent";
    ir.execution_semantics.preemption_priority = HierarchicalPriority::OuterFirst;

    StateNode parent("parent", "parent");
    parent.id = "parent_id";

    StateNode child("child", "child");
    child.id = "child_id";
    child.parent_id = "parent_id";

    StateNode dest("dest", "dest");
    dest.id = "dest_id";

    ir.states.push_back(parent);
    ir.states.push_back(child);
    ir.states.push_back(dest);

    // Parent transition with priority 2
    TransitionEdge tp;
    tp.source_id = "parent_id";
    tp.target_id = "dest_id";
    tp.trigger = SignalTrigger("RESET");
    tp.priority = 2;
    ir.add_transition(tp);

    // Child transition with priority 1 (higher precedence than parent, violating OuterFirst!)
    TransitionEdge tc;
    tc.source_id = "child_id";
    tc.target_id = "dest_id";
    tc.trigger = SignalTrigger("RESET");
    tc.priority = 1;
    ir.add_transition(tc);

    DiagnosticEngine diag;
    PriorityConflictPass pass;
    EXPECT_FALSE(pass.run(ir, diag));
    EXPECT_TRUE(diag.has_errors());

    bool found_e0402 = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "E0402")
            found_e0402 = true;
    }
    EXPECT_TRUE(found_e0402);
}

/**
 * @brief Verify PriorityConflictPass detects hierarchical preemption violations when hierarchy is defined via
 * parent_state name.
 */
TEST(PriorityConflict, ParentStateHierarchyStringName_DetectsHierarchicalPreemptionConflict) {
    FsmIr ir;
    ir.name = "ParentStateNameConflictModel";
    ir.initial_state = "parent";
    ir.execution_semantics.preemption_priority = HierarchicalPriority::OuterFirst;

    StateNode parent("parent", "parent");
    StateNode child("child", "child", "parent");  // sets parent_state = "parent", parent_id is empty
    StateNode dest("dest", "dest");

    ir.states.push_back(parent);
    ir.states.push_back(child);
    ir.states.push_back(dest);

    // Parent transition with priority 2
    TransitionEdge tp;
    tp.source = "parent";
    tp.target = "dest";
    tp.trigger = SignalTrigger("RESET");
    tp.priority = 2;
    ir.add_transition(tp);

    // Child transition with priority 1 (higher precedence than parent, violating OuterFirst!)
    TransitionEdge tc;
    tc.source = "child";
    tc.target = "dest";
    tc.trigger = SignalTrigger("RESET");
    tc.priority = 1;
    ir.add_transition(tc);

    DiagnosticEngine diag;
    PriorityConflictPass pass;
    EXPECT_FALSE(pass.run(ir, diag));
    EXPECT_TRUE(diag.has_errors());

    bool found_e0402 = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "E0402")
            found_e0402 = true;
    }
    EXPECT_TRUE(found_e0402);
}

/**
 * @brief Verify FsmValidator correctly accepts initial_state_id without const_cast mutation or false warnings.
 */
TEST(FsmValidator, InitialStateSpecifiedViaId_ValidatesCleanlyWithoutConstCastMutation) {
    FsmIr ir;
    ir.name = "IdBasedInitModel";

    StateNode s1("Idle", "Idle state");
    s1.id = "idle_unique_id";

    StateNode s2("Running", "Running state");
    s2.id = "running_unique_id";

    ir.states.push_back(s1);
    ir.states.push_back(s2);

    // Initial state specified ONLY via ID (initial_state is empty)
    ir.initial_state = "";
    ir.initial_state_id = "running_unique_id";

    TransitionEdge t;
    t.source_id = "running_unique_id";
    t.source = "Running";
    t.target_id = "idle_unique_id";
    t.target = "Idle";
    t.event = "Stop";
    ir.add_transition(t);

    auto result = FsmValidator::validate(ir);
    EXPECT_TRUE(result.is_valid);
    EXPECT_TRUE(result.errors.empty());

    // Must NOT have warning about defaulting to first state
    bool defaulted_warning = false;
    for (const auto& d : result.diagnostics) {
        if (d.category == "InitialState") {
            defaulted_warning = true;
        }
    }
    EXPECT_FALSE(defaulted_warning);

    // model.initial_state must NOT have been mutated by const_cast
    EXPECT_EQ(ir.initial_state, "");
}

// ============================================================================
// TimedInvariantsVerifierPass Tests
// ============================================================================

/**
 * @brief Verify TimedInvariantsVerifierPass validates states whose outgoing transitions respect stay permanence.
 * @scenario State invariant stays <= 500ms; outgoing timer transition fires after 200ms.
 * @expected Pass validates timing consistency with zero errors.
 */
TEST(TimedInvariantsVerifier, TransitionDelayWithinStatePermanence_PassesVerification) {
    FsmIr ir;
    ir.name = "ConsistentTimedModel";
    ir.initial_state_id = "s1";

    StateNode s1("s1", "s1");
    s1.time_invariant = StateTimeInvariant("stay_duration <= 500ms");
    StateNode s2("s2", "s2");
    ir.states.push_back(s1);
    ir.states.push_back(s2);

    // Outgoing transition after 200ms (200 <= 500ms, healthy!)
    TransitionEdge t;
    t.source_id = "s1";
    t.target_id = "s2";
    t.trigger = TimeTrigger(200, false);
    ir.add_transition(t);

    DiagnosticEngine diag;
    TimedInvariantsVerifierPass pass;
    EXPECT_TRUE(pass.run(ir, diag));
    EXPECT_FALSE(diag.has_errors());
}

/**
 * @brief Verify TimedInvariantsVerifierPass detects timelock where outgoing transition exceeds stay invariant.
 * @scenario State invariant stays <= 100ms; outgoing timer transition requires 300ms.
 * @expected Pass reports diagnostic error E0403 indicating timelock contradiction.
 */
TEST(TimedInvariantsVerifier, TransitionDelayExceedingPermanence_EmitsTimelockDiagnostic) {
    FsmIr ir;
    ir.name = "TimelockModel";
    ir.initial_state_id = "s1";

    StateNode s1("s1", "s1");
    // Stay at most 100ms
    s1.time_invariant = StateTimeInvariant("stay_duration <= 100ms");
    StateNode s2("s2", "s2");
    ir.states.push_back(s1);
    ir.states.push_back(s2);

    // Outgoing transition requires at least 300ms (300 > 100ms, impossible!)
    TransitionEdge t;
    t.source_id = "s1";
    t.target_id = "s2";
    t.trigger = TimeTrigger(300, false);
    ir.add_transition(t);

    DiagnosticEngine diag;
    TimedInvariantsVerifierPass pass;
    EXPECT_FALSE(pass.run(ir, diag));
    EXPECT_TRUE(diag.has_errors());

    bool found_e0403 = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "E0403")
            found_e0403 = true;
    }
    EXPECT_TRUE(found_e0403);
}

// ============================================================================
// EventQueueBoundPass Tests
// ============================================================================

/**
 * @brief Verify EventQueueBoundPass computes safe static queue capacity rounded up to a power of two.
 * @scenario Model with 3 deferred events and transitions emitting 2 concurrent signals.
 * @expected Safe queue bound calculated and stored in IR attributes as 8.
 */
TEST(EventQueueBound, DeferredEventsAndSignalEmits_CalculatesSafeCapacityPowerOfTwo) {
    FsmIr ir;
    ir.name = "QueueBoundModel";
    ir.initial_state_id = "s1";

    StateNode s1("s1", "s1");
    s1.deferred_events = {"EV_A", "EV_B", "EV_C"};  // 3 deferred events
    ir.states.push_back(s1);

    TransitionEdge t;
    t.source_id = "s1";
    t.target_id = "s1";
    t.trigger = SignalTrigger("TICK");

    ActionSignature act;
    SignalEmitOp emit1;
    emit1.signal_name = "ALERT_1";
    act.instructions.emplace_back(emit1);
    SignalEmitOp emit2;
    emit2.signal_name = "ALERT_2";
    act.instructions.emplace_back(emit2);
    t.transition_action = act;
    ir.add_transition(t);

    DiagnosticEngine diag;
    EventQueueBoundPass pass;
    EXPECT_TRUE(pass.run(ir, diag));

    // max_deferred = 3, max_emits = 2 -> bound = 3 + 2 + 2 = 7 -> rounded up to power of two: 8
    ASSERT_TRUE(ir.attributes.count("static_event_queue_capacity"));
    EXPECT_EQ(ir.attributes["static_event_queue_capacity"], "8");
}

/**
 * @file test_fsm_ir_semantic_completeness.cpp
 * @brief Unit tests for target-agnostic FSM IR semantic completeness extensions:
 *        Terminate pseudostate, Timed Automata ClockDefinition, ActionAstNode primitive
 *        instructions, ChangeTrigger, typed transition endpoints, and ExecutionSemantics.
 */

#include <gtest/gtest.h>

#include "fsm/ir/action.hpp"
#include "fsm/ir/clock_definition.hpp"
#include "fsm/ir/concurrency_semantics.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/state_kind.hpp"
#include "fsm/ir/state_node.hpp"
#include "fsm/ir/transition_edge.hpp"
#include "fsm/ir/trigger.hpp"

using namespace fsm::ir;

// ============================================================================
// 1. StateKind::Terminate Tests
// ============================================================================

/**
 * @brief Verify StateKind::Terminate pseudostate classification and string conversions.
 * @scenario State node configured with StateKind::Terminate.
 * @expected Terminate kind is distinct from Final and converts to string 'Terminate'.
 */
TEST(StateKind, TerminatePseudostate_DifferentiatedFromFinalState) {
    EXPECT_EQ(state_kind_to_string(StateKind::Terminate), "Terminate");
    EXPECT_EQ(state_kind_from_string("Terminate"), StateKind::Terminate);

    // Verify Terminate is distinct from Final
    EXPECT_NE(StateKind::Terminate, StateKind::Final);
    EXPECT_NE(state_kind_to_string(StateKind::Terminate), state_kind_to_string(StateKind::Final));

    StateNode term_node("KillSwitch", "Fatal stop", "");
    term_node.kind = StateKind::Terminate;
    EXPECT_EQ(term_node.kind, StateKind::Terminate);
}

// ============================================================================
// 2. Timed Automata ClockDefinition & ClockResetOp Tests
// ============================================================================

/**
 * @brief Verify ClockDefinition creation, resolution units, and clock reset operations.
 * @scenario Instantiation of ClockDefinition with Milliseconds resolution and associated ClockResetOp.
 * @expected Clock metadata and reset operators preserve identifiers and resolution semantics.
 */
TEST(ClockDefinition, TimedAutomataClocksAndResets_InitializesAndComparesCorrectly) {
    ClockDefinition clk1("clk_sensor", TimeUnit::Milliseconds, "Sensor polling timer");
    EXPECT_EQ(clk1.name, "clk_sensor");
    EXPECT_FALSE(clk1.id.empty());
    EXPECT_EQ(clk1.resolution, TimeUnit::Milliseconds);
    EXPECT_EQ(clk1.description, "Sensor polling timer");

    ClockDefinition clk2("clk_sensor", TimeUnit::Milliseconds, "Sensor polling timer");
    EXPECT_EQ(clk1, clk2);

    ClockResetOp reset_op("clk_sensor");
    EXPECT_EQ(reset_op.clock_name, "clk_sensor");
}

/**
 * @brief Verify multi-clock state timing invariants.
 * @scenario State 'ActiveMonitoring' configured with multiple clock bound expressions ('clk_sensor <= 500ms',
 * 'clk_timeout < 10000ms').
 * @expected Invariants are preserved in order within the state node's invariant list.
 */
TEST(StateNode, MultiClockInvariants_PreservesTimingExpressions) {
    StateNode timed_state("ActiveMonitoring", "Monitoring active state");

    // Add multi-clock invariants
    GuardAstNode inv1("clk_sensor <= 500ms");
    GuardAstNode inv2("clk_timeout < 10000ms");
    timed_state.invariants.push_back(inv1);
    timed_state.invariants.push_back(inv2);

    ASSERT_EQ(timed_state.invariants.size(), 2U);
    EXPECT_EQ(timed_state.invariants[0].to_string(), "clk_sensor <= 500ms");
    EXPECT_EQ(timed_state.invariants[1].to_string(), "clk_timeout < 10000ms");
}

// ============================================================================
// 3. ActionAstNode Primitive Instruction Suite
// ============================================================================

/**
 * @brief Verify ActionAstNode primitive instruction kinds (Store, PortWrite, PortRead, SignalEmit, ActionCall).
 * @scenario Instantiation of StoreOp, PortWriteOp, PortReadOp, SignalEmitOp, and ActionCallOp into ActionAstNode
 * variants.
 * @expected Operations populate variant alternative types and integrate into an ActionSignature sequence.
 */
TEST(ActionAstNode, PrimitiveInstructions_ConstructsAndIntegratesIntoActionSignature) {
    // 1. StoreOp
    StoreOp store_op;
    store_op.target = LValueTarget("reg_counter", LValueScope::Register);
    store_op.op = AssignmentOp::AddAssign;
    store_op.expression = "1";
    ActionAstNode store_node(store_op, "Increment step counter");
    EXPECT_EQ(store_node.kind, ActionOpKind::Store);
    ASSERT_TRUE(std::holds_alternative<StoreOp>(store_node.op));
    EXPECT_EQ(std::get<StoreOp>(store_node.op).target.name, "reg_counter");
    EXPECT_EQ(std::get<StoreOp>(store_node.op).op, AssignmentOp::AddAssign);

    // 2. PortWriteOp
    PortWriteOp write_op;
    write_op.port_name = "telemetry_bus";
    write_op.expression = "payload.status";
    write_op.is_latched = true;
    ActionAstNode write_node(write_op, "Emit telemetry packet");
    EXPECT_EQ(write_node.kind, ActionOpKind::PortWrite);
    ASSERT_TRUE(std::holds_alternative<PortWriteOp>(write_node.op));
    EXPECT_EQ(std::get<PortWriteOp>(write_node.op).port_name, "telemetry_bus");
    EXPECT_TRUE(std::get<PortWriteOp>(write_node.op).is_latched);

    // 3. PortReadOp
    PortReadOp read_op;
    read_op.port_name = "sensor_in";
    read_op.destination = LValueTarget("reg_raw", LValueScope::Register);
    ActionAstNode read_node(read_op, "Sample sensor input");
    EXPECT_EQ(read_node.kind, ActionOpKind::PortRead);
    ASSERT_TRUE(std::holds_alternative<PortReadOp>(read_node.op));
    EXPECT_EQ(std::get<PortReadOp>(read_node.op).port_name, "sensor_in");

    // 4. SignalEmitOp
    SignalEmitOp emit_op;
    emit_op.signal_name = "EvCriticalAlert";
    emit_op.arguments = {"404", "true"};
    emit_op.target_port = "alarm_port";
    ActionAstNode emit_node(emit_op, "Broadcast alarm");
    EXPECT_EQ(emit_node.kind, ActionOpKind::SignalEmit);
    ASSERT_TRUE(std::holds_alternative<SignalEmitOp>(emit_node.op));
    EXPECT_EQ(std::get<SignalEmitOp>(emit_node.op).signal_name, "EvCriticalAlert");
    EXPECT_EQ(std::get<SignalEmitOp>(emit_node.op).arguments.size(), 2U);

    // 5. ActionCallOp with explicit read/write sets
    ActionCallOp call_op;
    call_op.function_name = "calibrate_imu";
    call_op.arguments = {"bias_correction"};
    call_op.read_set = {"reg_accel_x", "reg_accel_y"};
    call_op.write_set = {"reg_calibrated"};
    ActionAstNode call_node(call_op, "Calibrate IMU hardware");
    EXPECT_EQ(call_node.kind, ActionOpKind::ActionCall);
    ASSERT_TRUE(std::holds_alternative<ActionCallOp>(call_node.op));
    EXPECT_EQ(std::get<ActionCallOp>(call_node.op).function_name, "calibrate_imu");
    EXPECT_EQ(std::get<ActionCallOp>(call_node.op).read_set.size(), 2U);
    EXPECT_EQ(std::get<ActionCallOp>(call_node.op).write_set.size(), 1U);

    // Integration into ActionSignature
    ActionSignature sig("process_step");
    sig.instructions.push_back(store_node);
    sig.instructions.push_back(write_node);
    sig.instructions.push_back(emit_node);
    EXPECT_FALSE(sig.empty());
    EXPECT_EQ(sig.instructions.size(), 3U);
}

// ============================================================================
// 4. Continuous ChangeTrigger Tests
// ============================================================================

/**
 * @brief Verify ChangeTrigger continuous predicate semantics and encapsulation.
 * @scenario ChangeTrigger initialized with relational expression 'altitude_ft > 10000 && speed_kts > 250'.
 * @expected TriggerVariant encapsulates ChangeTrigger and formats trigger name as 'when(...)'.
 */
TEST(ChangeTrigger, ContinuousSignalPredicates_EncapsulatesBooleanExpressions) {
    ChangeTrigger ct("altitude_ft > 10000 && speed_kts > 250");
    EXPECT_EQ(ct.raw_expression, "altitude_ft > 10000 && speed_kts > 250");
    EXPECT_TRUE(ct.active_on_true);
    ASSERT_TRUE(ct.predicate.has_value());

    // Wrap in TriggerVariant
    TriggerVariant tv = ct;
    ASSERT_TRUE(std::holds_alternative<ChangeTrigger>(tv));

    TransitionEdge edge("t1", "LowAlt", "HighAlt", tv);
    EXPECT_EQ(edge.get_trigger_name(), "when(altitude_ft > 10000 && speed_kts > 250)");
}

// ============================================================================
// 5. TransitionEdge Typed Endpoints & Clock Resets Tests
// ============================================================================

/**
 * @brief Verify TransitionEdge deterministic ID computation and clock reset vector tracking.
 * @scenario Transition edge with source 'Init', target 'Standby', and clock resets 'stay_clk' and 't_watchdog'.
 * @expected Deterministic UUID hashes synthesized for endpoints and clock resets recorded.
 */
TEST(TransitionEdge, TypedEndpointsAndClockResets_ComputesDeterministicIds) {
    TransitionEdge edge("Init", "Standby", "EvStart");
    EXPECT_FALSE(edge.source_id.empty());
    EXPECT_FALSE(edge.target_id.empty());
    EXPECT_EQ(edge.source_id, compute_deterministic_id("Init"));
    EXPECT_EQ(edge.target_id, compute_deterministic_id("Standby"));
    ASSERT_EQ(edge.multi_source_ids.size(), 1U);
    ASSERT_EQ(edge.multi_target_ids.size(), 1U);

    edge.clock_resets.push_back("stay_clk");
    edge.clock_resets.push_back("t_watchdog");
    ASSERT_EQ(edge.clock_resets.size(), 2U);
    EXPECT_EQ(edge.clock_resets[0], "stay_clk");
    EXPECT_EQ(edge.clock_resets[1], "t_watchdog");
}

// ============================================================================
// 6. ExecutionSemantics & HierarchicalPriority Tests
// ============================================================================

/**
 * @brief Verify ExecutionSemantics validation and preemption priority serialization.
 * @scenario ExecutionSemantics configuration with RunToCompletion, OuterFirst, and Synchronous scheduling.
 * @expected Valid configuration returns is_valid == true; incompatible semantics detected and flagged invalid.
 */
TEST(ExecutionSemantics, DispatchModelAndPreemptionPolicy_ValidatesConfigurationConsistency) {
    EXPECT_EQ(hierarchical_priority_to_string(HierarchicalPriority::OuterFirst), "OuterFirst");
    EXPECT_EQ(hierarchical_priority_to_string(HierarchicalPriority::InnerFirst), "InnerFirst");
    EXPECT_EQ(string_to_hierarchical_priority("OuterFirst"), HierarchicalPriority::OuterFirst);
    EXPECT_EQ(string_to_hierarchical_priority("InnerFirst"), HierarchicalPriority::InnerFirst);

    ExecutionSemantics sem;
    sem.dispatch_model = EventDispatchSemantics::SingleEventRunToCompletion;
    sem.preemption_priority = HierarchicalPriority::OuterFirst;
    sem.orthogonal_scheduling = OrthogonalConflictResolution::SimultaneousSynchronous;
    sem.datapath_isolation = DatapathIsolation::SharedDatapath;
    EXPECT_TRUE(sem.is_valid());

    // Invalid combination: synchronous reactive cannot use asynchronous interleaving
    sem.dispatch_model = EventDispatchSemantics::SynchronousReactive;
    sem.orthogonal_scheduling = OrthogonalConflictResolution::Interleaved;
    EXPECT_FALSE(sem.is_valid());
}

// ============================================================================
// 7. Full FsmIr Integration Test
// ============================================================================

/**
 * @brief Verify comprehensive FsmIr model integration with timed automata clocks, terminate states, and port actions.
 * @scenario Full avionics FSM model featuring ClockDefinition, StateKind::Terminate, ChangeTrigger, and PortWriteOp.
 * @expected Entire model structure preserves semantics, types, and relationships faithfully.
 */
TEST(FsmIr, SemanticCompletenessModel_IntegratesClocksTerminatesAndContinuousTriggers) {
    FsmIr model;
    model.name = "AvionicsMissionManager";

    // Configure ExecutionSemantics
    model.execution_semantics.dispatch_model = EventDispatchSemantics::SingleEventRunToCompletion;
    model.execution_semantics.preemption_priority = HierarchicalPriority::OuterFirst;

    // Define continuous clock
    ClockDefinition mission_clock("mission_time", TimeUnit::Seconds, "Global mission elapsed time");
    model.clocks.push_back(mission_clock);
    ASSERT_EQ(model.clocks.size(), 1U);

    // States
    StateNode s_init("Booting", "Initial boot phase");
    StateNode s_active("MissionActive", "Flight control loop");
    s_active.invariants.emplace_back("mission_time <= 7200s");

    StateNode s_term("FatalShutdown", "Immediate abort");
    s_term.kind = StateKind::Terminate;

    model.states.push_back(s_init);
    model.states.push_back(s_active);
    model.states.push_back(s_term);

    // Transition with ChangeTrigger and ClockReset
    ChangeTrigger flameout_trigger("flameout_detected == true");
    TransitionEdge abort_trans("abort_edge", "MissionActive", "FatalShutdown", flameout_trigger, 1);
    abort_trans.clock_resets.push_back("mission_time");

    // Action with primitive instructions
    ActionSignature abort_act("abort_action");
    PortWriteOp cut_fuel;
    cut_fuel.port_name = "fuel_valve";
    cut_fuel.expression = "0";
    cut_fuel.is_latched = true;
    abort_act.instructions.emplace_back(cut_fuel);
    abort_trans.set_action(abort_act);

    model.transitions.push_back(abort_trans);

    // Verify model integrity
    EXPECT_EQ(model.states.size(), 3U);
    EXPECT_EQ(model.transitions.size(), 1U);
    const auto* t = &model.transitions[0];
    EXPECT_EQ(t->source, "MissionActive");
    EXPECT_EQ(t->target, "FatalShutdown");
    EXPECT_EQ(t->clock_resets.size(), 1U);
    EXPECT_TRUE(t->transition_action.has_value());
    EXPECT_EQ(t->transition_action->instructions.size(), 1U);
    EXPECT_EQ(t->transition_action->instructions[0].kind, ActionOpKind::PortWrite);
}

/**
 * @file test_efsm_metamodel_extensions.cpp
 * @brief Unit tests for EFSM metamodel extensions: LValueTarget, ActionAssignment, StateTimeInvariant, and
 * ConcurrencySemantics.
 */

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "fsm/ir/action.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/ir/state_node.hpp"
#include "fsm/ir/transition_edge.hpp"

using namespace fsm::ir;

namespace {

/**
 * @brief Verify LValueTarget scope qualification, member paths, and array index parsing.
 * @scenario Target identifiers with scopes ('local.temp', 'reg.status', 'out.command_word', 'reg.sensors[3]').
 * @expected LValueTarget parses scopes, nested member paths, and integer array subscript expressions.
 */
TEST(LValueTarget, QualifiedPathAndScopingSyntax_ParsesRegistersLocalsAndPorts) {
    // 1. Unqualified register variable
    LValueTarget t_unqual("counter");
    EXPECT_EQ(t_unqual.scope, LValueScope::Register);
    EXPECT_EQ(t_unqual.name, "counter");
    EXPECT_TRUE(t_unqual.member_path.empty());
    EXPECT_FALSE(t_unqual.constant_index.has_value());
    EXPECT_EQ(t_unqual.full_path(), "counter");
    EXPECT_EQ(t_unqual, "counter");

    // 2. Explicit local variable
    LValueTarget t_local("local.temp");
    EXPECT_EQ(t_local.scope, LValueScope::Local);
    EXPECT_EQ(t_local.name, "temp");
    EXPECT_EQ(t_local.full_path(), "temp");
    EXPECT_EQ(t_local.scoped_path(), "local.temp");
    EXPECT_EQ(t_local, "local.temp");

    // 3. Register scope prefix
    LValueTarget t_reg("reg.status");
    EXPECT_EQ(t_reg.scope, LValueScope::Register);
    EXPECT_EQ(t_reg.name, "status");
    EXPECT_EQ(t_reg.full_path(), "status");
    EXPECT_EQ(t_reg.scoped_path(), "reg.status");
    EXPECT_EQ(t_reg, "reg.status");

    // 4. OutPort scope prefix
    LValueTarget t_port("out.command_word");
    EXPECT_EQ(t_port.scope, LValueScope::OutPort);
    EXPECT_EQ(t_port.name, "command_word");
    EXPECT_EQ(t_port.full_path(), "command_word");
    EXPECT_EQ(t_port.scoped_path(), "out.command_word");
    EXPECT_EQ(t_port, "out.command_word");

    // 4. Nested member path
    LValueTarget t_member("reg.subsystem.telemetry.temp");
    EXPECT_EQ(t_member.scope, LValueScope::Register);
    EXPECT_EQ(t_member.name, "subsystem");
    ASSERT_EQ(t_member.member_path.size(), 2u);
    EXPECT_EQ(t_member.member_path[0], "telemetry");
    EXPECT_EQ(t_member.full_path(), "subsystem.telemetry.temp");
    EXPECT_EQ(t_member.scoped_path(), "reg.subsystem.telemetry.temp");
    EXPECT_EQ(t_member, "reg.subsystem.telemetry.temp");

    // 5. Array indexing
    LValueTarget t_arr("reg.sensors[3]");
    EXPECT_EQ(t_arr.scope, LValueScope::Register);
    EXPECT_EQ(t_arr.name, "sensors");
    ASSERT_TRUE(t_arr.constant_index.has_value());
    EXPECT_EQ(*t_arr.constant_index, 3u);
    EXPECT_EQ(t_arr.full_path(), "sensors[3]");
    EXPECT_EQ(t_arr.scoped_path(), "reg.sensors[3]");
    EXPECT_EQ(t_arr, "reg.sensors[3]");
}

/**
 * @brief Verify ActionAssignment construction and assignment statement parsing.
 * @scenario Parse assignment statements ('reg.speed += speed + 10', 'out.valve_pos = 100;').
 * @expected ActionAssignment extracts target, assignment operator, and right-hand side expression.
 */
TEST(ActionAssignment, AssignmentStatementsAndOperators_ExtractsLValueAndRValueExpressions) {
    ActionAssignment assign("reg.speed", "speed + 10", AssignmentOp::AddAssign);
    EXPECT_EQ(assign.target.scope, LValueScope::Register);
    EXPECT_EQ(assign.target.name, "speed");
    EXPECT_EQ(assign.target, "speed");
    EXPECT_EQ(assign.op, AssignmentOp::AddAssign);
    EXPECT_EQ(assign.expression, "speed + 10");

    // Parsing assignment statement
    auto parsed = ActionAssignment::parse("out.valve_pos = 100;");
    EXPECT_EQ(parsed.target.scope, LValueScope::OutPort);
    EXPECT_EQ(parsed.target.name, "valve_pos");
    EXPECT_EQ(parsed.op, AssignmentOp::Assign);
    EXPECT_EQ(parsed.expression, "100");
}

/**
 * @brief Verify StateTimeInvariant parsing, duration units, and state integration.
 * @scenario Strings 'stay <= 250ms', 'stay_duration < 10s', and '500[ms]'.
 * @expected Time invariant structures populate clock name, duration magnitude, comparison operator, and time units.
 */
TEST(StateTimeInvariant, TimeBoundExpressionsAndUnits_ParsesDurationAndOperators) {
    // 1. stay <= 250ms
    StateTimeInvariant inv1("stay <= 250ms");
    EXPECT_EQ(inv1.clock, "stay");
    EXPECT_EQ(inv1.op, TimeInvariantOp::LessEqual);
    EXPECT_EQ(inv1.duration, 250u);
    EXPECT_EQ(inv1.unit, TimeUnit::Milliseconds);
    EXPECT_EQ(inv1, "stay <= 250ms");

    // 2. stay_duration < 10s
    StateTimeInvariant inv2("stay_duration < 10s");
    EXPECT_EQ(inv2.clock, "stay_duration");
    EXPECT_EQ(inv2.op, TimeInvariantOp::LessThan);
    EXPECT_EQ(inv2.duration, 10u);
    EXPECT_EQ(inv2.unit, TimeUnit::Seconds);

    // 3. Short duration string with bracket unit
    StateTimeInvariant inv3("500[ms]");
    EXPECT_EQ(inv3.clock, "stay_duration");
    EXPECT_EQ(inv3.op, TimeInvariantOp::LessEqual);
    EXPECT_EQ(inv3.duration, 500u);
    EXPECT_EQ(inv3.unit, TimeUnit::Milliseconds);

    // 4. Stream and string operator+ concatenation
    std::ostringstream ss;
    ss << inv1;
    EXPECT_EQ(ss.str(), "stay <= 250ms");
    std::string cat = "Invariant: " + inv1;
    EXPECT_EQ(cat, "Invariant: stay <= 250ms");

    // 5. StateNode integration
    StateNode node("Active");
    node.time_invariant = StateTimeInvariant("stay <= 100ms");
    ASSERT_TRUE(node.time_invariant.has_value());
    EXPECT_EQ(*node.time_invariant, "stay <= 100ms");
}

/**
 * @brief Verify ConcurrencySemantics configuration combinations, validity matrix, and JSON roundtrip.
 * @scenario Test valid and invalid combinations of dispatch model, orthogonal scheduling, and datapath isolation.
 * @expected Concurrency validation flags invalid combinations (e.g., synchronous reactive with interleaved execution).
 */
TEST(ConcurrencySemantics, ExecutionModelMatrixAndValidation_EnforcesCoherentSemantics) {
    // 1. EventDispatchSemantics
    EXPECT_EQ(event_dispatch_semantics_to_string(EventDispatchSemantics::SingleEventRunToCompletion),
              "SingleEventRunToCompletion");
    EXPECT_EQ(event_dispatch_semantics_to_string(EventDispatchSemantics::SynchronousReactive), "SynchronousReactive");
    EXPECT_EQ(event_dispatch_semantics_to_string(EventDispatchSemantics::ActiveObjectAsynchronous),
              "ActiveObjectAsynchronous");

    EXPECT_EQ(string_to_event_dispatch_semantics("SingleEventRunToCompletion"),
              EventDispatchSemantics::SingleEventRunToCompletion);
    EXPECT_EQ(string_to_event_dispatch_semantics("SynchronousReactive"), EventDispatchSemantics::SynchronousReactive);
    EXPECT_EQ(string_to_event_dispatch_semantics("ActiveObjectAsynchronous"),
              EventDispatchSemantics::ActiveObjectAsynchronous);

    // 2. OrthogonalConflictResolution
    EXPECT_EQ(orthogonal_conflict_resolution_to_string(OrthogonalConflictResolution::Interleaved), "Interleaved");
    EXPECT_EQ(orthogonal_conflict_resolution_to_string(OrthogonalConflictResolution::SimultaneousSynchronous),
              "SimultaneousSynchronous");
    EXPECT_EQ(orthogonal_conflict_resolution_to_string(OrthogonalConflictResolution::PriorityOrdered),
              "PriorityOrdered");
    EXPECT_EQ(orthogonal_conflict_resolution_to_string(OrthogonalConflictResolution::DocumentOrder), "DocumentOrder");

    EXPECT_EQ(string_to_orthogonal_conflict_resolution("Interleaved"), OrthogonalConflictResolution::Interleaved);
    EXPECT_EQ(string_to_orthogonal_conflict_resolution("SimultaneousSynchronous"),
              OrthogonalConflictResolution::SimultaneousSynchronous);
    EXPECT_EQ(string_to_orthogonal_conflict_resolution("PriorityOrdered"),
              OrthogonalConflictResolution::PriorityOrdered);
    EXPECT_EQ(string_to_orthogonal_conflict_resolution("DocumentOrder"), OrthogonalConflictResolution::DocumentOrder);

    // 3. DatapathIsolation
    EXPECT_EQ(datapath_isolation_to_string(DatapathIsolation::SharedDatapath), "SharedDatapath");
    EXPECT_EQ(datapath_isolation_to_string(DatapathIsolation::PartitionedDatapath), "PartitionedDatapath");

    EXPECT_EQ(string_to_datapath_isolation("SharedDatapath"), DatapathIsolation::SharedDatapath);
    EXPECT_EQ(string_to_datapath_isolation("PartitionedDatapath"), DatapathIsolation::PartitionedDatapath);

    // 4. Semantic validity matrix (is_valid)
    ConcurrencySemantics valid_sync{EventDispatchSemantics::SynchronousReactive,
                                    OrthogonalConflictResolution::SimultaneousSynchronous,
                                    DatapathIsolation::SharedDatapath};
    EXPECT_TRUE(valid_sync.is_valid());

    ConcurrencySemantics invalid_sync{EventDispatchSemantics::SynchronousReactive,
                                      OrthogonalConflictResolution::Interleaved, DatapathIsolation::SharedDatapath};
    EXPECT_FALSE(invalid_sync.is_valid());

    ConcurrencySemantics scxml_concurrency{EventDispatchSemantics::SingleEventRunToCompletion,
                                           OrthogonalConflictResolution::DocumentOrder,
                                           DatapathIsolation::SharedDatapath};
    EXPECT_TRUE(scxml_concurrency.is_valid());

    // 5. ConcurrencySemantics integration and JSON serialization
    FsmIr ir;
    ir.name = "FormalConcurrencyModel";
    ir.concurrency.dispatch = EventDispatchSemantics::SynchronousReactive;
    ir.concurrency.orthogonal_conflict = OrthogonalConflictResolution::PriorityOrdered;
    ir.concurrency.datapath_isolation = DatapathIsolation::PartitionedDatapath;
    ir.add_state("Idle");
    ir.initial_state = "Idle";

    std::string well_formed_err;
    EXPECT_TRUE(ir.is_well_formed(well_formed_err));

    // Invalidate concurrency and verify well-formedness fails
    ir.concurrency.orthogonal_conflict = OrthogonalConflictResolution::Interleaved;
    EXPECT_FALSE(ir.is_well_formed(well_formed_err));
    EXPECT_NE(well_formed_err.find("Invalid concurrency semantics"), std::string::npos);

    // Restore valid concurrency
    ir.concurrency.orthogonal_conflict = OrthogonalConflictResolution::PriorityOrdered;

    std::string json = FsmIrSerializer::serialize_json(ir);
    EXPECT_NE(json.find("\"concurrency\": {"), std::string::npos);
    EXPECT_NE(json.find("\"dispatch\": \"SynchronousReactive\""), std::string::npos);
    EXPECT_NE(json.find("\"orthogonal_conflict\": \"PriorityOrdered\""), std::string::npos);
    EXPECT_NE(json.find("\"datapath_isolation\": \"PartitionedDatapath\""), std::string::npos);

    // 6. OrthogonalRegion priority
    OrthogonalRegion reg1{"r1", "Region1", "S1", {"S1", "S2"}, 1u};
    OrthogonalRegion reg2{"r2", "Region2", "S3", {"S3", "S4"}, 2u};
    EXPECT_EQ(reg1.priority, 1u);
    EXPECT_EQ(reg2.priority, 2u);
    EXPECT_NE(reg1, reg2);
}

/**
 * @brief Verify dual transition actions (condition and transition effects) and priority canonicalization.
 * @scenario Multiple transitions with mixed priorities (0, 1, 2) and separate condition/transition actions.
 * @expected Canonicalize sorts edges by strict priority order (1, then 2, then default 0) preserving action signatures.
 */
TEST(TransitionEdge, ConditionActionsAndPriorities_SortsCanonicallyByAscendingPriority) {
    FsmIr ir;
    ir.name = "DualActionAndPriorityModel";
    ir.add_state("S0");
    ir.add_state("S1");
    ir.add_state("S2");
    ir.add_state("S3");
    ir.initial_state = "S0";

    // Transition with both condition_action and transition_action
    TransitionEdge t_dual("S0", "S1", "EvStep");
    t_dual.condition_action = ActionSignature("EvalGuardEffect");
    t_dual.transition_action = ActionSignature("TransitionEffect");
    t_dual.priority = 2;

    // Transition with higher priority (1 is higher than 2)
    TransitionEdge t_high("S0", "S2", "EvStep");
    t_high.set_action("HighPriorityEffect");
    t_high.priority = 1;

    // Transition with default priority (0 is lowest)
    TransitionEdge t_def("S0", "S3", "EvStep");
    t_def.set_action("DefaultEffect");
    t_def.priority = 0;

    ir.add_transition(t_dual);
    ir.add_transition(t_def);
    ir.add_transition(t_high);

    // Prior to canonicalize, transitions are in insertion order
    EXPECT_EQ(ir.transitions[0].priority, 2u);
    EXPECT_EQ(ir.transitions[1].priority, 0u);
    EXPECT_EQ(ir.transitions[2].priority, 1u);

    // Canonicalize sorts transitions by priority: 1, then 2, then 0
    ir.canonicalize();

    ASSERT_EQ(ir.transitions.size(), 3u);
    EXPECT_EQ(ir.transitions[0].priority, 1u);
    EXPECT_EQ(ir.transitions[0].target, "S2");

    EXPECT_EQ(ir.transitions[1].priority, 2u);
    EXPECT_EQ(ir.transitions[1].target, "S1");
    ASSERT_TRUE(ir.transitions[1].condition_action.has_value());
    EXPECT_EQ(ir.transitions[1].condition_action->name, "EvalGuardEffect");
    ASSERT_TRUE(ir.transitions[1].transition_action.has_value());
    EXPECT_EQ(ir.transitions[1].transition_action->name, "TransitionEffect");

    EXPECT_EQ(ir.transitions[2].priority, 0u);
    EXPECT_EQ(ir.transitions[2].target, "S3");
}

}  // namespace

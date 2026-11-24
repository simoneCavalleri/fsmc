/**
 * @file test_efsm_interval_analysis.cpp
 * @brief Unit tests for EFSMIntervalAnalyzer abstract interpretation and port interval contracts.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend::analysis;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify EFSM interval analysis validates port domain bounds and detects contract violations.
 * @scenario Define InPort 'sensor_val' in [0, 100] and OutPort 'actuator_cmd' in [0, 200].
 * @expected Pass confirms contract adherence without reporting errors on compliant models.
 */
TEST(EfsmIntervalAnalysis, CompliantPortBounds_PassesWithoutErrors) {
    FsmIr model;
    model.name = "BoundedFSM";

    PortDefinition in_p("sensor_val", "float", PortDirection::In);
    in_p.min_value = 0.0;
    in_p.max_value = 100.0;
    model.ports.push_back(in_p);

    PortDefinition out_p("actuator_cmd", "float", PortDirection::Out);
    out_p.min_value = 0.0;
    out_p.max_value = 200.0;
    model.ports.push_back(out_p);

    DiagnosticEngine diag;
    EFSMIntervalAnalyzer interval_pass(model);
    auto findings = interval_pass.analyze(diag);
    EXPECT_FALSE(diag.has_errors());
}

/**
 * @brief Verify EFSM interval analyzer detects out-of-range assignments violating OutPort contracts.
 * @scenario Define OutPort 'heater_power' in range [0.0, 100.0] and assign value 150.0f.
 * @expected Analyzer emits W_PORT_RANGE_VIOLATION diagnostic flagging out-of-range assignment.
 */
TEST(EfsmIntervalAnalysis, OutOfRangePortAssignment_EmitsPortRangeViolation) {
    FsmIr model;
    model.name = "ThermostatFSM";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Heating");

    PortDefinition out_p("heater_power", "float", PortDirection::Out);
    out_p.min_value = 0.0;
    out_p.max_value = 100.0;
    model.ports.push_back(out_p);

    TransitionEdge t("t1", "Idle", "Heating", SignalTrigger("EvStart"));
    ActionSignature act("OverheatAct", "OverheatAct");
    act.assignments.push_back({"heater_power", "150.0f"});
    t.transition_action = act;
    model.add_transition(t);

    DiagnosticEngine diag;
    EFSMIntervalAnalyzer analyzer(model);
    auto findings = analyzer.analyze(diag);

    bool found_port_violation = false;
    for (const auto& f : findings) {
        if (f.variable_name == "heater_power" && f.is_error) {
            found_port_violation = true;
            break;
        }
    }
    EXPECT_TRUE(found_port_violation);
}

/**
 * @brief Verify EFSM interval analyzer detects unsatisfiable guards over bounded InPorts.
 * @scenario Define InPort 'sensor_temp' in [-50.0, 50.0] with guard 'in.sensor_temp > 90.0f'.
 * @expected Analyzer identifies unsatisfiable guard condition and reports diagnostic finding.
 */
TEST(EfsmIntervalAnalysis, GuardOutsidePortDomain_EmitsUnsatisfiableDiagnostic) {
    FsmIr model;
    model.name = "SensorFSM";
    model.initial_state = "Active";
    model.add_state("Active");
    model.add_state("Alert");

    PortDefinition in_p("sensor_temp", "float", PortDirection::In);
    in_p.min_value = -50.0;
    in_p.max_value = 50.0;
    model.ports.push_back(in_p);

    GuardModel gm("OverheatGuard", "OverheatGuard", "in.sensor_temp > 90.0f", "in.sensor_temp > 90.0f");
    model.guards.push_back(gm);

    TransitionEdge t("t1", "Active", "Alert", AnonymousTrigger{});
    t.guard = "OverheatGuard";
    model.add_transition(t);

    DiagnosticEngine diag;
    EFSMIntervalAnalyzer analyzer(model);
    auto findings = analyzer.analyze(diag);

    bool found_unsat_guard = false;
    for (const auto& f : findings) {
        if (f.variable_name == "sensor_temp" && !f.is_error) {
            found_unsat_guard = true;
            break;
        }
    }
    EXPECT_TRUE(found_unsat_guard);
}

TEST(EFSMIntervalAnalysis, SignalPayloadValidatorPropagatesBoundedIntervalToRegister) {
    FsmIr model;
    model.name = "PayloadIntervalFSM";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Receiving");

    VariableDefinition reg_len;
    reg_len.name = "last_len";
    reg_len.type = DataType{PrimitiveTypeKind::UInt32};
    reg_len.initial_value = "0";
    model.add_variable(std::move(reg_len));

    SignalDefinition sig("Packet");
    sig.attributes.emplace_back("len", DataType{PrimitiveTypeKind::UInt32}, "64");
    sig.validators.push_back("len >= 10 && len <= 1500");
    model.add_signal(sig);

    TransitionEdge t("t1", "Idle", "Receiving", SignalTrigger{"Packet", "payload"});
    ActionSignature act("capture_len");
    ActionAssignment assign;
    assign.target = LValueTarget("last_len", LValueScope::Register);
    assign.op = AssignmentOp::Assign;
    assign.expression = "payload.len";
    act.assignments.push_back(assign);
    t.set_action(act);
    model.add_transition(t);

    EFSMIntervalAnalyzer analyzer(model);
    auto envs = analyzer.compute_state_intervals();

    ASSERT_TRUE(envs.count("Receiving") > 0);
    ASSERT_TRUE(envs.at("Receiving").count("last_len") > 0);
    const Interval& iv = envs.at("Receiving").at("last_len");
    EXPECT_DOUBLE_EQ(iv.lo, 10.0);
    EXPECT_DOUBLE_EQ(iv.hi, 1500.0);
}

TEST(EFSMIntervalAnalysis, MultipleValidatorsAndGuardIntersectToTightInterval) {
    FsmIr model;
    model.name = "TightPayloadIntervalFSM";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Bounded");

    VariableDefinition reg_val;
    reg_val.name = "clamped_val";
    reg_val.type = DataType{PrimitiveTypeKind::UInt32};
    reg_val.initial_value = "0";
    model.add_variable(std::move(reg_val));

    SignalDefinition sig("Sample");
    sig.attributes.emplace_back("val", DataType{PrimitiveTypeKind::UInt32}, "20");
    sig.validators.push_back("val >= 10");
    sig.validators.push_back("val <= 100");
    model.add_signal(sig);

    TransitionEdge t("t1", "Idle", "Bounded", SignalTrigger{"Sample", "payload"});
    t.set_guard("payload.val <= 50");
    ActionSignature act("store_val");
    ActionAssignment assign;
    assign.target = LValueTarget("clamped_val", LValueScope::Register);
    assign.op = AssignmentOp::Assign;
    assign.expression = "payload.val";
    act.assignments.push_back(assign);
    t.set_action(act);
    model.add_transition(t);

    EFSMIntervalAnalyzer analyzer(model);
    auto envs = analyzer.compute_state_intervals();

    ASSERT_TRUE(envs.count("Bounded") > 0);
    ASSERT_TRUE(envs.at("Bounded").count("clamped_val") > 0);
    const Interval& iv = envs.at("Bounded").at("clamped_val");
    EXPECT_DOUBLE_EQ(iv.lo, 10.0);
    EXPECT_DOUBLE_EQ(iv.hi, 50.0);
}

/**
 * @brief Verify Interval equality with infinities and 0 * inf multiplication do not produce NaN.
 * @scenario Compare unbounded/half-bounded intervals and multiply zero interval by unbounded interval.
 * @expected Equal infinite bounds compare equal; multiplication with 0.0 yields exact [0, 0].
 */
TEST(EFSMIntervalAnalysis, IntervalEqualityAndZeroTimesInfinity_SoundWithoutNaN) {
    const double inf = std::numeric_limits<double>::infinity();
    Interval unbounded;
    Interval unbounded2;
    EXPECT_EQ(unbounded, unbounded2);

    Interval half_lo(0.0, inf);
    Interval half_lo2(0.0, inf);
    EXPECT_EQ(half_lo, half_lo2);
    EXPECT_NE(half_lo, unbounded);

    Interval empty1(5.0, 1.0);
    Interval empty2(2.0, -3.0);
    EXPECT_EQ(empty1, empty2);
    EXPECT_NE(empty1, unbounded);

    Interval zero_iv(0.0, 0.0);
    Interval prod_scalar = unbounded.mul(0.0);
    EXPECT_FALSE(std::isnan(prod_scalar.lo));
    EXPECT_FALSE(std::isnan(prod_scalar.hi));
    EXPECT_DOUBLE_EQ(prod_scalar.lo, 0.0);
    EXPECT_DOUBLE_EQ(prod_scalar.hi, 0.0);

    Interval prod_iv = zero_iv.mul(unbounded);
    EXPECT_FALSE(std::isnan(prod_iv.lo));
    EXPECT_FALSE(std::isnan(prod_iv.hi));
    EXPECT_DOUBLE_EQ(prod_iv.lo, 0.0);
    EXPECT_DOUBLE_EQ(prod_iv.hi, 0.0);
}

/**
 * @brief Verify integer strict inequality interval analysis detects unsatisfiable adjacent integer bounds.
 * @scenario Integer variable 'x' in [0, 10] with transition guard 'x > 5 && x < 6'.
 * @expected Strict integer bounds [6, +inf] and [-inf, 5] have empty intersection and report UnsatisfiableGuard.
 */
TEST(EFSMIntervalAnalysis, IntegerStrictInequality_DetectsUnsatisfiableAdjacentBounds) {
    FsmIr model;
    model.name = "IntegerStrictGuardFSM";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Unreachable");
    model.add_state("Safe");

    PortDefinition in_x("x", DataType{PrimitiveTypeKind::Int32}, PortDirection::In);
    in_x.min_value = 0.0;
    in_x.max_value = 100.0;
    model.ports.push_back(in_x);

    TransitionEdge t_dead("t_dead", "Idle", "Unreachable", SignalTrigger{"Check", ""});
    t_dead.set_guard("x > 5 && x < 6");
    model.add_transition(t_dead);

    TransitionEdge t_safe("t_safe", "Idle", "Safe", SignalTrigger{"GoSafe", ""});
    t_safe.set_guard("x < 50");
    ActionSignature act("scale_x");
    act.assignments.emplace_back(LValueTarget("scaled_x", LValueScope::Register), "(x + 2) * 3");
    t_safe.set_action(act);
    model.add_transition(t_safe);

    VariableDefinition scaled_var("scaled_x", DataType{PrimitiveTypeKind::Int32}, "0");
    model.add_variable(scaled_var);

    DiagnosticEngine diag;
    EFSMIntervalAnalyzer analyzer(model);
    auto findings = analyzer.analyze(diag);

    bool found_unsat = false;
    for (const auto& f : findings) {
        if (f.transition_id == "t_dead" && f.kind == EFSMFindingKind::UnsatisfiableGuard) {
            found_unsat = true;
            break;
        }
    }
    EXPECT_TRUE(found_unsat);

    auto envs = analyzer.compute_state_intervals();
    ASSERT_TRUE(envs.count("Safe") > 0);
    EXPECT_DOUBLE_EQ(envs.at("Safe").at("x").lo, 0.0);
    EXPECT_DOUBLE_EQ(envs.at("Safe").at("x").hi, 49.0);
    EXPECT_DOUBLE_EQ(envs.at("Safe").at("scaled_x").lo, 6.0);
    EXPECT_DOUBLE_EQ(envs.at("Safe").at("scaled_x").hi, 153.0);
}

/**
 * @brief Verify EFSMIntervalAnalyzer propagates variable updates across composite state hierarchy.
 * @scenario Variable count=0 incremented in substate Step1 -> Step2, then checked on parent group transition Active ->
 * Off.
 * @expected Guard 'count > 0' on Active -> Off is NOT falsely flagged as UnsatisfiableGuard.
 */
TEST(EFSMIntervalAnalysisTest, PropagatesIntervalsAcrossCompositeHierarchy) {
    FsmIr ir;
    ir.name = "HierInterval";
    ir.initial_state = "Off";

    VariableDefinition cnt("count", DataType(PrimitiveTypeKind::UInt32), "0");
    ir.variables.push_back(cnt);

    ir.add_state("Off");
    auto& active = ir.add_state("Active");
    active.is_composite = true;
    active.initial_sub_state = "Step1";
    ir.add_state("Step1", "Active");
    ir.add_state("Step2", "Active");
    ir.add_state("Done");

    ir.add_transition(TransitionEdge("Off", "Active", "EvOn"));

    ActionSignature inc_act("inc");
    StoreOp st;
    st.target = LValueTarget("count", LValueScope::Register);
    st.op = AssignmentOp::Assign;
    st.expression = "count + 1";
    inc_act.instructions.emplace_back(st);
    ir.add_transition(TransitionEdge("Step1", "Step2", "EvNext", std::nullopt, inc_act));

    TransitionEdge check_t("Active", "Done", "EvCheck", "count > 0");
    check_t.id = "t_hier_check";
    ir.add_transition(check_t);

    EFSMIntervalAnalyzer analyzer(ir);
    DiagnosticEngine diag;
    auto findings = analyzer.analyze(diag);

    for (const auto& f : findings) {
        EXPECT_NE(f.transition_id, "t_hier_check");
    }
}

/**
 * @brief Verify EFSMIntervalAnalyzer applies state entry_actions before checking outgoing guards.
 * @scenario Variable x=0 updated by Idle.entry_actions (x = 1), then checked by Idle -> Done [x == 1].
 * @expected Guard 'x == 1' is NOT falsely flagged as UnsatisfiableGuard.
 */
TEST(EFSMIntervalAnalysisTest, AppliesStateEntryActionsToStateEnvironment) {
    FsmIr ir;
    ir.name = "EntryInterval";
    ir.initial_state = "Idle";
    ir.variables.emplace_back("x", DataType(PrimitiveTypeKind::Int32), "0");

    auto& idle = ir.add_state("Idle");
    ActionSignature entry_act("init_x");
    StoreOp st;
    st.target = LValueTarget("x", LValueScope::Register);
    st.op = AssignmentOp::Assign;
    st.expression = "1";
    entry_act.instructions.emplace_back(st);
    idle.entry_actions.push_back(entry_act);
    ir.add_state("Done");

    TransitionEdge t("Idle", "Done", "EvGo", "x == 1");
    t.id = "t_entry_check";
    ir.add_transition(t);

    EFSMIntervalAnalyzer analyzer(ir);
    DiagnosticEngine diag;
    auto findings = analyzer.analyze(diag);

    for (const auto& f : findings) {
        EXPECT_NE(f.transition_id, "t_entry_check");
    }
}

/**
 * @brief Verify EFSMIntervalAnalyzer propagates variable intervals through orthogonal regions.
 * @scenario Parallel state 'Par' has two orthogonal regions Reg1 (R1A -> R1B with x = x + 1) and
 * Reg2 (R2A -> R2B with x = x + 1), and an outgoing transition 'Par -> Done [x >= 2]'.
 * @expected Guard 'x >= 2' is NOT falsely flagged as UnsatisfiableGuard.
 */
TEST(EFSMIntervalAnalysisTest, PropagatesIntervalsAcrossOrthogonalRegions) {
    FsmIr ir;
    ir.name = "OrthogonalInterval";
    ir.initial_state = "Par";
    ir.variables.emplace_back("x", DataType(PrimitiveTypeKind::Int32), "0");

    auto& par = ir.add_state("Par");
    par.is_composite = true;
    par.kind = StateKind::Parallel;
    par.orthogonal_regions.emplace_back("Reg1", "Reg1", "R1A");
    par.orthogonal_regions.emplace_back("Reg2", "Reg2", "R2A");

    auto& reg1 = ir.add_state("Reg1", "Par");
    reg1.is_composite = true;
    reg1.initial_sub_state = "R1A";
    ir.add_state("R1A", "Reg1");
    ir.add_state("R1B", "Reg1");

    auto& reg2 = ir.add_state("Reg2", "Par");
    reg2.is_composite = true;
    reg2.initial_sub_state = "R2A";
    ir.add_state("R2A", "Reg2");
    ir.add_state("R2B", "Reg2");

    ir.add_state("Done");

    ActionSignature inc_x("inc_x");
    StoreOp st;
    st.target = LValueTarget("x", LValueScope::Register);
    st.op = AssignmentOp::Assign;
    st.expression = "x + 1";
    inc_x.instructions.emplace_back(st);

    ir.add_transition(TransitionEdge("R1A", "R1B", "E1", std::nullopt, inc_x));
    ir.add_transition(TransitionEdge("R1B", "R1A", "E1"));
    ir.add_transition(TransitionEdge("R2A", "R2B", "E2", std::nullopt, inc_x));
    ir.add_transition(TransitionEdge("R2B", "R2A", "E2"));

    TransitionEdge check_t("Par", "Done", "STOP", "x >= 2");
    check_t.id = "t_ortho_check";
    ir.add_transition(check_t);

    EFSMIntervalAnalyzer analyzer(ir);
    DiagnosticEngine diag;
    auto findings = analyzer.analyze(diag);

    for (const auto& f : findings) {
        EXPECT_NE(f.transition_id, "t_ortho_check");
    }
}

/**
 * @brief Verify EFSMIntervalAnalyzer applies source state exit_actions along non-internal transitions.
 * @scenario Variable count=0 incremented by Idle.exit_actions (count = count + 1) when transitioning
 * Idle -> Active, then checked by Active -> Done [count >= 1].
 * @expected Guard 'count >= 1' is NOT falsely flagged as UnsatisfiableGuard.
 */
TEST(EFSMIntervalAnalysisTest, AppliesStateExitActionsAcrossTransitions) {
    FsmIr ir;
    ir.name = "ExitInterval";
    ir.initial_state = "Idle";
    ir.variables.emplace_back("count", DataType(PrimitiveTypeKind::Int32), "0");

    auto& idle = ir.add_state("Idle");
    ActionSignature exit_act("inc_on_exit");
    exit_act.assignments.emplace_back(LValueTarget("count", LValueScope::Register), "count + 1");
    idle.exit_actions.push_back(exit_act);

    ir.add_state("Active");
    ir.add_state("Done");

    ir.add_transition(TransitionEdge("Idle", "Active", "EvStart"));
    TransitionEdge check_t("Active", "Done", "EvFinish", "count >= 1");
    check_t.id = "t_exit_check";
    ir.add_transition(check_t);

    EFSMIntervalAnalyzer analyzer(ir);
    DiagnosticEngine diag;
    auto findings = analyzer.analyze(diag);

    for (const auto& f : findings) {
        EXPECT_NE(f.transition_id, "t_exit_check");
    }
}

/**
 * @brief Regression (F2.1): Verify negated guard domain `!(x > 5)` is properly inverted to `x <= 5`
 * and compound negations `!(x > 5 && y > 5)` are handled conservatively.
 */
TEST(EFSMIntervalAnalysisTest, NegatedGuardDomain_ProperlyInvertsComparisonAndHandlesCompoundNegation) {
    FsmIr ir;
    ir.name = "NegatedGuardModel";
    ir.initial_state = "Init";
    ir.variables.emplace_back("x", DataType(PrimitiveTypeKind::Int32), "10");
    ir.variables.emplace_back("y", DataType(PrimitiveTypeKind::Int32), "0");

    ir.add_state("Init");
    ir.add_state("DeadTarget");
    ir.add_state("LiveTarget");

    // With x == 10, "!(x > 5)" means "x <= 5", which is unsatisfiable!
    TransitionEdge t_dead("Init", "DeadTarget", "EvCheck", "!(x > 5)");
    t_dead.id = "t_neg_dead";
    ir.add_transition(t_dead);

    // With x == 10, y == 0, "!(x > 5 && y > 5)" IS satisfiable (since y <= 5): must NOT be falsely flagged!
    TransitionEdge t_live("Init", "LiveTarget", "EvCompound", "!(x > 5 && y > 5)");
    t_live.id = "t_neg_live";
    ir.add_transition(t_live);

    EFSMIntervalAnalyzer analyzer(ir);
    DiagnosticEngine diag;
    auto findings = analyzer.analyze(diag);

    bool found_dead = false;
    bool false_live = false;
    for (const auto& f : findings) {
        if (f.transition_id == "t_neg_dead" && f.kind == EFSMFindingKind::UnsatisfiableGuard) {
            found_dead = true;
        }
        if (f.transition_id == "t_neg_live") {
            false_live = true;
        }
    }
    EXPECT_TRUE(found_dead);
    EXPECT_FALSE(false_live);
}

}  // namespace

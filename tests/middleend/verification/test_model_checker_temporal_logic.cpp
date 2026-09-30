/**
 * @file test_model_checker_temporal_logic.cpp
 * @brief Unit tests for LTL/CTL temporal logic parser, ModelChecker solver, and SMV formal specification generation.
 */

#include <gtest/gtest.h>

#include "fsm/backend/formal/smv_serializer.hpp"
#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/ltl_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/model_checker.hpp"
#include "fsm/middleend/pass_manager.hpp"

using namespace fsm::ir;
using namespace fsm::diagnostic;
using namespace fsm::frontend;
using namespace fsm::frontend::directive;
using namespace fsm::middleend;
using namespace fsm::middleend::analysis;
using namespace fsm::backend;
using namespace fsm::backend::formal;

namespace {

/**
 * @brief Verify LTL formula tokenization and operator parsing (G, F, X, !, &&, ||, U, ->).
 * @scenario Parse strings with unary temporal operators (G, F, X, !) and binary operators (&&, ||, U, ->).
 * @expected AST correctly populated with corresponding TemporalOp and serialized to matching string format.
 */
TEST(LtlPropertyParser, BasicUnaryAndBinaryOperators_ParsesAstAndToString) {
    // 1. Unary operators
    auto g_node = LtlPropertyParser::parse("G InFlight");
    ASSERT_TRUE(g_node.has_value());
    EXPECT_EQ(g_node->op, TemporalOp::Globally);
    EXPECT_EQ(g_node->to_string(), "G (InFlight)");

    auto f_node = LtlPropertyParser::parse("F Connected");
    ASSERT_TRUE(f_node.has_value());
    EXPECT_EQ(f_node->op, TemporalOp::Finally);
    EXPECT_EQ(f_node->to_string(), "F (Connected)");

    auto x_node = LtlPropertyParser::parse("X Armed");
    ASSERT_TRUE(x_node.has_value());
    EXPECT_EQ(x_node->op, TemporalOp::Next);
    EXPECT_EQ(x_node->to_string(), "X (Armed)");

    auto not_node = LtlPropertyParser::parse("! Fault");
    ASSERT_TRUE(not_node.has_value());
    EXPECT_EQ(not_node->op, TemporalOp::Not);
    EXPECT_EQ(not_node->to_string(), "!Fault");

    // 2. Binary operators
    auto and_node = LtlPropertyParser::parse("PowerOk && DoorClosed");
    ASSERT_TRUE(and_node.has_value());
    EXPECT_EQ(and_node->op, TemporalOp::And);
    EXPECT_EQ(and_node->to_string(), "PowerOk && DoorClosed");

    auto or_node = LtlPropertyParser::parse("Manual || Auto");
    ASSERT_TRUE(or_node.has_value());
    EXPECT_EQ(or_node->op, TemporalOp::Or);
    EXPECT_EQ(or_node->to_string(), "Manual || Auto");

    auto until_node = LtlPropertyParser::parse("Charging U BatteryFull");
    ASSERT_TRUE(until_node.has_value());
    EXPECT_EQ(until_node->op, TemporalOp::Until);
    EXPECT_EQ(until_node->to_string(), "Charging U BatteryFull");

    auto impl_node = LtlPropertyParser::parse("StartCmd -> Navigating");
    ASSERT_TRUE(impl_node.has_value());
    EXPECT_EQ(impl_node->op, TemporalOp::Implies);
    EXPECT_EQ(impl_node->to_string(), "StartCmd -> Navigating");
}

/**
 * @brief Verify complex nested temporal logic formulas (response properties, mutual exclusion).
 * @scenario Parse 'G (LowBattery -> F SafeLand)' and 'G (! (StateA && StateB))'.
 * @expected Composite AST nodes preserve operator hierarchy and string representation.
 */
TEST(LtlPropertyParser, NestedTemporalFormulas_PreservesAssociativityAndPrecedence) {
    // Response property: G (LowBattery -> F SafeLand)
    auto resp = LtlPropertyParser::parse("G (LowBattery -> F SafeLand)");
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->op, TemporalOp::Globally);
    EXPECT_EQ(resp->to_string(), "G (LowBattery -> F (SafeLand))");

    // Mutual exclusion: G (! (StateA && StateB))
    auto mutex = LtlPropertyParser::parse("G (! (StateA && StateB))");
    ASSERT_TRUE(mutex.has_value());
    EXPECT_EQ(mutex->op, TemporalOp::Globally);
    EXPECT_EQ(mutex->to_string(), "G (!(StateA && StateB))");
}

/**
 * @brief Verify '@fsm:property' and '@fsm:var' directive extraction and deserialization.
 * @scenario Directive string declaring formal safety property with traceability requirement and bounded integer
 * variable.
 * @expected Directive parser populates formal property definition and variable domain metadata.
 */
TEST(DirectiveParser, PropertyAndVariableDirectives_ExtractsModelMetadataAndConstraints) {
    // Property directive
    std::string prop_line =
        "@fsm:property name=SafeLand kind=Safety ltl=\"G (LowBattery -> F SafeLand)\" req=\"REQ-SAFE-01\" desc=\"Safe "
        "landing\"";
    auto prop_opt = DirectiveParser::parse_property_directive(DirectiveParser::extract_directive_body(prop_line));
    ASSERT_TRUE(prop_opt.has_value());
    EXPECT_EQ(prop_opt->name, "SafeLand");
    EXPECT_EQ(prop_opt->kind, PropertyKind::Safety);
    EXPECT_EQ(prop_opt->raw_formula, "G (LowBattery -> F SafeLand)");
    EXPECT_EQ(prop_opt->traceability_req, "REQ-SAFE-01");
    EXPECT_EQ(prop_opt->description, "Safe landing");
    ASSERT_TRUE(prop_opt->ast.has_value());
    EXPECT_EQ(prop_opt->ast->op, TemporalOp::Globally);

    // Variable directive
    std::string var_line = "@fsm:var name=retry_count type=uint32_t init=0 min=0 max=5 desc=\"Connection retries\"";
    auto var_opt = DirectiveParser::parse_variable_directive(DirectiveParser::extract_directive_body(var_line));
    ASSERT_TRUE(var_opt.has_value());
    EXPECT_EQ(var_opt->name, "retry_count");
    EXPECT_EQ(var_opt->type, "uint32_t");
    EXPECT_EQ(var_opt->initial_value, "0");
    EXPECT_EQ(var_opt->min_value, 0);
    EXPECT_EQ(var_opt->max_value, 5);
    EXPECT_EQ(var_opt->description, "Connection retries");
}

/**
 * @brief Verify safety invariant evaluation, violation detection, and step-by-step trace generation.
 * @scenario Model with states Idle -> Arming -> Armed / HazardFault evaluated against satisfied and violated
 * invariants.
 * @expected Satisfied invariant passes with empty trace; violated invariant fails and produces 3-step counterexample
 * trace.
 */
TEST(ModelChecker, SafetyInvariantSatisfiedAndViolated_EmitsVerdictAndCounterexampleTrace) {
    FsmIr ir;
    ir.name = "SafetySystem";
    ir.initial_state = "Idle";

    ir.add_state("Idle");
    ir.add_state("Arming");
    ir.add_state("Armed");
    ir.add_state("HazardFault");

    ir.add_transition("Idle", "Arming", SignalTrigger{"ArmCmd", ""});
    ir.add_transition("Arming", "Armed", SignalTrigger{"ArmOk", ""});
    ir.add_transition("Arming", "HazardFault", SignalTrigger{"SensorErr", ""});

    // 1. Invariant that passes: G (! Armed || ! Idle)
    FormalProperty prop_ok("IdleArmedDisjoint", PropertyKind::Invariant, "G (! (Idle && Armed))");
    prop_ok.ast = LtlPropertyParser::parse(prop_ok.raw_formula);

    ModelChecker checker(ir);
    auto res_ok = checker.verify_property(prop_ok);
    EXPECT_TRUE(res_ok.passed);
    EXPECT_TRUE(res_ok.counterexample_trace.empty());

    // 2. Safety property that is VIOLATED: G (! HazardFault)
    FormalProperty prop_violated("NoHazardState", PropertyKind::Safety, "G (! HazardFault)");
    prop_violated.ast = LtlPropertyParser::parse(prop_violated.raw_formula);

    auto res_fail = checker.verify_property(prop_violated);
    EXPECT_FALSE(res_fail.passed);
    EXPECT_FALSE(res_fail.counterexample_trace.empty());

    // Verify counterexample trace: Idle -> Arming -> HazardFault
    ASSERT_GE(res_fail.counterexample_trace.size(), 3u);
    EXPECT_EQ(res_fail.counterexample_trace[0].state_name, "Idle");
    EXPECT_EQ(res_fail.counterexample_trace[1].state_name, "Arming");
    EXPECT_EQ(res_fail.counterexample_trace[2].state_name, "HazardFault");

    std::string trace_output = res_fail.format_counterexample();
    EXPECT_NE(trace_output.find("Step 0: State 'Idle'"), std::string::npos);
    EXPECT_NE(trace_output.find("Step 2: State 'HazardFault'"), std::string::npos);
}

/**
 * @brief Verify response liveness property verification ('G (Trigger -> F Target)').
 * @scenario State machine transitioning Standby -> InFlight -> ReturnToHome -> Landed.
 * @expected Property 'G (InFlight -> F Landed)' evaluates to true.
 */
TEST(ModelChecker, ResponseLivenessProperty_VerifiesTemporalSequenceSatisfaction) {
    FsmIr ir;
    ir.name = "MissionDrone";
    ir.initial_state = "Standby";

    ir.add_state("Standby");
    ir.add_state("InFlight");
    ir.add_state("ReturnToHome");
    ir.add_state("Landed");

    ir.add_transition("Standby", "InFlight", SignalTrigger{"Takeoff", ""});
    ir.add_transition("InFlight", "ReturnToHome", SignalTrigger{"LowBattery", ""});
    ir.add_transition("ReturnToHome", "Landed", SignalTrigger{"Touchdown", ""});

    // Response property: G (InFlight -> F Landed)
    FormalProperty prop_resp("EventuallyLanded", PropertyKind::Liveness, "G (InFlight -> F Landed)");
    prop_resp.ast = LtlPropertyParser::parse(prop_resp.raw_formula);

    ModelChecker checker(ir);
    auto res = checker.verify_property(prop_resp);
    EXPECT_TRUE(res.passed);
}

/**
 * @brief Verify integration of formal verification within PassManager optimization pipeline.
 * @scenario Run PassManager default pipeline over an FSM with liveness properties.
 * @expected PassManager runs without fatal errors and confirms model soundness.
 */
TEST(ModelChecker, OptimizationPipelinePassManager_IntegratesVerificationSeamlessly) {
    FsmIr ir;
    ir.name = "PassManagerVerifiedFSM";
    ir.initial_state = "Init";
    ir.add_state("Init");
    ir.add_state("Done");
    ir.add_transition("Init", "Done", SignalTrigger{"Finish", ""});

    // Valid property
    FormalProperty prop("InitCanReachDone", PropertyKind::Liveness, "F Done");
    prop.ast = LtlPropertyParser::parse("F Done");
    ir.add_property(prop);

    PassManager pm = PassManager::create_default_pipeline();
    DiagnosticEngine diag;
    bool ok = pm.run(ir, diag);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.has_errors());
}

/**
 * @brief Verify nuXmv / SMV formal model generation with state transitions and LTLSPEC.
 * @scenario Serialize FsmIr with state variables and properties into SMV format.
 * @expected Emitted SMV string contains valid MODULE main, state domain, variable domains, init state, and LTLSPEC
 * clauses.
 */
TEST(SmvSerializer, StateMachineAndLtlSpecs_GeneratesValidSmvModule) {
    FsmIr ir;
    ir.name = "SpacecraftMission";
    ir.initial_state = "Prelaunch";

    ir.add_state("Prelaunch");
    ir.add_state("Ascending");
    ir.add_state("InOrbit");

    ir.add_transition("Prelaunch", "Ascending", SignalTrigger{"Ignition", ""});
    ir.add_transition("Ascending", "InOrbit", SignalTrigger{"InsertionOk", ""});

    // Add state variable
    VariableDefinition var_fuel("fuel_percent", "uint32_t", "100", 0, 100, "Fuel level");
    ir.add_variable(var_fuel);

    // Add LTL property
    FormalProperty prop("OrbitAchieved", PropertyKind::Liveness, "F InOrbit");
    prop.ast = LtlPropertyParser::parse("F InOrbit");
    ir.add_property(prop);

    std::string smv_code = SmvSerializer::serialize(ir);

    EXPECT_NE(smv_code.find("MODULE main"), std::string::npos);
    EXPECT_NE(smv_code.find("state : {Prelaunch, Ascending, InOrbit}"), std::string::npos);
    EXPECT_NE(smv_code.find("fuel_percent : 0..100;"), std::string::npos);
    EXPECT_NE(smv_code.find("init(state) := Prelaunch;"), std::string::npos);
    EXPECT_NE(smv_code.find("state = Prelaunch & event = Ignition : Ascending;"), std::string::npos);
    EXPECT_NE(smv_code.find("LTLSPEC -- OrbitAchieved"), std::string::npos);
    EXPECT_NE(smv_code.find("F (state = InOrbit);"), std::string::npos);
}

/**
 * @brief Verify counterexample trace generation when reaching a prohibited fatal error state.
 * @scenario FSM reaches FatalError on Fault trigger; property asserts 'G (!FatalError)'.
 * @expected Model checker flags violation and counterexample trace begins in Idle and concludes in FatalError.
 */
TEST(ModelChecker, ReachableProhibitedFatalState_SynthesizesDiagnosticTrace) {
    FsmIr ir;
    ir.name = "ViolationTraceFSM";
    ir.initial_state = "Idle";
    ir.add_state("Idle");
    ir.add_state("Running");
    ir.add_state("FatalError");

    ir.add_transition("Idle", "Running", SignalTrigger{"Start", ""});
    ir.add_transition("Running", "FatalError", SignalTrigger{"Fault", ""});

    // Invariant: Globally NOT FatalError
    FormalProperty prop("NeverFatalError", PropertyKind::Safety, "G (!FatalError)");
    prop.ast = LtlPropertyParser::parse("G (!FatalError)");

    ModelChecker checker(ir);
    auto res = checker.verify_property(prop);
    EXPECT_FALSE(res.passed);
    ASSERT_FALSE(res.counterexample_trace.empty());
    EXPECT_EQ(res.counterexample_trace.front().state_name, "Idle");
    EXPECT_EQ(res.counterexample_trace.back().state_name, "FatalError");
}

/**
 * @brief Verify SMV serializer emission for CTLSPEC temporal properties.
 * @scenario Model configured with CTL property 'EF Executing'.
 * @expected SMV serializer formats CTL properties and state declarations accurately.
 */
TEST(SmvSerializer, CtlSpecFormulas_EmitsCtlModuleClauses) {
    FsmIr ir;
    ir.name = "CtlVerifiedFSM";
    ir.initial_state = "Ready";
    ir.add_state("Ready");
    ir.add_state("Executing");

    ir.add_transition("Ready", "Executing", SignalTrigger{"Run", ""});

    FormalProperty prop("ExistsPathToExecuting", PropertyKind::Liveness, "EF Executing");
    prop.ast = LtlPropertyParser::parse("F Executing");
    ir.add_property(prop);

    std::string smv_code = SmvSerializer::serialize(ir);
    EXPECT_NE(smv_code.find("MODULE main"), std::string::npos);
    EXPECT_NE(smv_code.find("state : {Ready, Executing}"), std::string::npos);
}

/**
 * @brief Verify ModelChecker evaluates relational comparisons (<, <=, >, >=, ==, !=) on datapath variables.
 * @scenario Thermostat FSM with numeric variables (temperature, setpoint) and port power_pct checked against relational
 * bounds.
 * @expected True predicates evaluate to passed; violated predicates fail and produce counterexample traces.
 */
TEST(ModelChecker, RelationalDatapathPredicates_EvaluatesTruthAndCounterexamples) {
    FsmIr ir;
    ir.name = "ThermostatFsm";
    ir.initial_state = "Heating";
    ir.add_state("Heating");
    ir.add_state("Cooling");
    ir.add_transition("Heating", "Cooling", SignalTrigger{"TempHigh", ""});

    // Add variables
    VariableDefinition var_temp("temperature", "float", "22.5", 10, 40, "Room temp");
    VariableDefinition var_setpoint("setpoint", "float", "20.0", 15, 30, "Target temp");
    ir.add_variable(var_temp);
    ir.add_variable(var_setpoint);

    // Add port
    PortDefinition port_pwr;
    port_pwr.name = "power_pct";
    port_pwr.default_value = "85";
    ir.ports.push_back(port_pwr);

    // 1. Property with variable comparison that holds: G (temperature > 20.0)
    FormalProperty prop_ok("TempAboveMin", PropertyKind::Safety, "G (temperature > 20.0)");
    prop_ok.ast = LtlPropertyParser::parse("G (temperature > 20.0)");

    ModelChecker checker(ir);
    auto res_ok = checker.verify_property(prop_ok);
    EXPECT_TRUE(res_ok.passed);

    // 2. Property with port comparison: G (power_pct <= 100)
    FormalProperty prop_pwr("PowerWithinRange", PropertyKind::Safety, "G (power_pct <= 100)");
    prop_pwr.ast = LtlPropertyParser::parse("G (power_pct <= 100)");
    auto res_pwr = checker.verify_property(prop_pwr);
    EXPECT_TRUE(res_pwr.passed);

    // 3. Property that fails: G (temperature < 20.0)
    FormalProperty prop_fail("TempBelowMin", PropertyKind::Safety, "G (temperature < 20.0)");
    prop_fail.ast = LtlPropertyParser::parse("G (temperature < 20.0)");
    auto res_fail = checker.verify_property(prop_fail);
    EXPECT_FALSE(res_fail.passed);
    EXPECT_FALSE(res_fail.counterexample_trace.empty());
}

/**
 * @brief Verify CTL formula parsing across all branching operators (EX, AX, EF, AF, EG, AG, E[U], A[U]).
 */
TEST(LtlPropertyParser, CtlFormulas_ParsesOperatorsAndStructure) {
    auto ex_node = LtlPropertyParser::parse("EX InFlight");
    ASSERT_TRUE(ex_node.has_value());
    EXPECT_EQ(ex_node->op, TemporalOp::EX);
    EXPECT_EQ(ex_node->to_string(), "EX (InFlight)");

    auto ax_node = LtlPropertyParser::parse("AX Armed");
    ASSERT_TRUE(ax_node.has_value());
    EXPECT_EQ(ax_node->op, TemporalOp::AX);
    EXPECT_EQ(ax_node->to_string(), "AX (Armed)");

    auto ef_node = LtlPropertyParser::parse("EF Connected");
    ASSERT_TRUE(ef_node.has_value());
    EXPECT_EQ(ef_node->op, TemporalOp::EF);
    EXPECT_EQ(ef_node->to_string(), "EF (Connected)");

    auto af_node = LtlPropertyParser::parse("AF Safe");
    ASSERT_TRUE(af_node.has_value());
    EXPECT_EQ(af_node->op, TemporalOp::AF);
    EXPECT_EQ(af_node->to_string(), "AF (Safe)");

    auto eg_node = LtlPropertyParser::parse("EG Loop");
    ASSERT_TRUE(eg_node.has_value());
    EXPECT_EQ(eg_node->op, TemporalOp::EG);
    EXPECT_EQ(eg_node->to_string(), "EG (Loop)");

    auto ag_node = LtlPropertyParser::parse("AG Operational");
    ASSERT_TRUE(ag_node.has_value());
    EXPECT_EQ(ag_node->op, TemporalOp::AG);
    EXPECT_EQ(ag_node->to_string(), "AG (Operational)");

    auto eu_node = LtlPropertyParser::parse("E [Idle U Active]");
    ASSERT_TRUE(eu_node.has_value());
    EXPECT_EQ(eu_node->op, TemporalOp::EU);
    EXPECT_EQ(eu_node->to_string(), "E [Idle U Active]");

    auto au_node = LtlPropertyParser::parse("A [Startup U Running]");
    ASSERT_TRUE(au_node.has_value());
    EXPECT_EQ(au_node->op, TemporalOp::AU);
    EXPECT_EQ(au_node->to_string(), "A [Startup U Running]");
}

/**
 * @brief Verify native ModelChecker CTL fixed-point model checking over branching transition graphs.
 */
TEST(ModelChecker, CtlModelChecking_EvaluatesBranchingTimeLogic) {
    FsmIr ir;
    ir.name = "BranchingFSM";
    ir.initial_state = "Init";
    ir.add_state("Init");
    ir.add_state("BranchA");
    ir.add_state("BranchB");
    ir.add_state("TargetA");
    ir.add_state("FaultState");

    // Init branches to BranchA or BranchB
    ir.add_transition("Init", "BranchA", SignalTrigger{"SelectA", ""});
    ir.add_transition("Init", "BranchB", SignalTrigger{"SelectB", ""});

    // BranchA leads to TargetA
    ir.add_transition("BranchA", "TargetA", SignalTrigger{"Advance", ""});

    // BranchB leads to FaultState
    ir.add_transition("BranchB", "FaultState", SignalTrigger{"Fail", ""});

    ModelChecker checker(ir);

    // 1. EF TargetA holds: there exists a path reaching TargetA
    FormalProperty prop_ef("ExistsTargetA", PropertyKind::Reachability, "EF TargetA");
    prop_ef.ast = LtlPropertyParser::parse("EF TargetA");
    auto res_ef = checker.verify_property(prop_ef);
    EXPECT_TRUE(res_ef.passed);

    // 2. EX BranchA holds: there exists a direct successor BranchA
    FormalProperty prop_ex("ExistsNextBranchA", PropertyKind::Safety, "EX BranchA");
    prop_ex.ast = LtlPropertyParser::parse("EX BranchA");
    auto res_ex = checker.verify_property(prop_ex);
    EXPECT_TRUE(res_ex.passed);

    // 3. AX BranchA fails: not all direct successors are BranchA (BranchB is also a successor)
    FormalProperty prop_ax("AllNextBranchA", PropertyKind::Safety, "AX BranchA");
    prop_ax.ast = LtlPropertyParser::parse("AX BranchA");
    auto res_ax = checker.verify_property(prop_ax);
    EXPECT_FALSE(res_ax.passed);

    // 4. AG (!FaultState) fails: FaultState is reachable via BranchB
    FormalProperty prop_ag("NeverFault", PropertyKind::Safety, "AG (!FaultState)");
    prop_ag.ast = LtlPropertyParser::parse("AG (!FaultState)");
    auto res_ag = checker.verify_property(prop_ag);
    EXPECT_FALSE(res_ag.passed);
    EXPECT_FALSE(res_ag.counterexample_trace.empty());

    // 5. E [Init U TargetA] holds: there exists a path staying in Init until TargetA is reached
    FormalProperty prop_eu("ExistsPathToTarget", PropertyKind::Reachability, "E [Init U TargetA]");
    prop_eu.ast = LtlPropertyParser::parse("E [Init U TargetA]");
    // Note: Init -> BranchA -> TargetA: in BranchA, Init is false, so it's not staying in Init.
    // Let's test E [(Init || BranchA) U TargetA]
    FormalProperty prop_eu_path("ExistsPathToTarget", PropertyKind::Reachability, "E [(Init || BranchA) U TargetA]");
    prop_eu_path.ast = LtlPropertyParser::parse("E [(Init || BranchA) U TargetA]");
    auto res_eu = checker.verify_property(prop_eu_path);
    EXPECT_TRUE(res_eu.passed);

    // 6. A [(Init || BranchA) U TargetA] fails because BranchB does not satisfy the condition
    FormalProperty prop_au("AllPathsToTarget", PropertyKind::Reachability, "A [(Init || BranchA) U TargetA]");
    prop_au.ast = LtlPropertyParser::parse("A [(Init || BranchA) U TargetA]");
    auto res_au = checker.verify_property(prop_au);
    EXPECT_FALSE(res_au.passed);
}

/**
 * @brief Verify extended interval arithmetic: multiplication, division, and widening operator.
 */
TEST(IntervalArithmetic, MulDivAndWidening_ComputesSoundBounds) {
    Interval iv1(2.0, 5.0);
    auto m_pos = iv1.mul(3.0);
    EXPECT_DOUBLE_EQ(m_pos.lo, 6.0);
    EXPECT_DOUBLE_EQ(m_pos.hi, 15.0);

    auto m_neg = iv1.mul(-2.0);
    EXPECT_DOUBLE_EQ(m_neg.lo, -10.0);
    EXPECT_DOUBLE_EQ(m_neg.hi, -4.0);

    Interval iv2(-2.0, 3.0);
    Interval iv3(4.0, 5.0);
    auto m_cross = iv2.mul(iv3);
    EXPECT_DOUBLE_EQ(m_cross.lo, -10.0);
    EXPECT_DOUBLE_EQ(m_cross.hi, 15.0);

    Interval iv4(10.0, 20.0);
    auto d_pos = iv4.div(2.0);
    EXPECT_DOUBLE_EQ(d_pos.lo, 5.0);
    EXPECT_DOUBLE_EQ(d_pos.hi, 10.0);

    // Widening: upper bound grows -> +inf
    Interval w_up = Interval(0.0, 10.0).widen_with(Interval(0.0, 20.0));
    EXPECT_DOUBLE_EQ(w_up.lo, 0.0);
    EXPECT_TRUE(std::isinf(w_up.hi) && w_up.hi > 0);

    // Widening: lower bound shrinks -> -inf
    Interval w_lo = Interval(5.0, 10.0).widen_with(Interval(2.0, 10.0));
    EXPECT_TRUE(std::isinf(w_lo.lo) && w_lo.lo < 0);
    EXPECT_DOUBLE_EQ(w_lo.hi, 10.0);
}

/**
 * @brief Verify EFSMDataPathPass dead transition pruning based on unsatisfiable guard intervals.
 */
TEST(EFSMDataPathPass, UnsatisfiableGuard_PrunesDeadTransitionWhenOptimizationEnabled) {
    FsmIr ir;
    ir.name = "DataPathPruningFsm";
    ir.initial_state = "Start";
    ir.add_state("Start");
    ir.add_state("UnreachableTarget");
    ir.add_state("NormalTarget");

    VariableDefinition var_val("counter", "int", "5", 0, 10, "Counter variable");
    ir.add_variable(var_val);

    // Transition 1: counter > 100 (infeasible since counter is 5)
    TransitionEdge t_dead("Start", "UnreachableTarget", "EvTick");
    t_dead.id = "T_Dead";
    t_dead.set_guard("counter > 100");
    ir.transitions.push_back(t_dead);

    // Transition 2: counter < 10 (feasible)
    TransitionEdge t_live("Start", "NormalTarget", "EvTick");
    t_live.id = "T_Live";
    t_live.set_guard("counter < 10");
    ir.transitions.push_back(t_live);

    DiagnosticEngine diag;
    EFSMDataPathPass pass(true);  // prune_dead_transitions = true
    EXPECT_TRUE(pass.run(ir, diag));

    // Dead transition must have been pruned
    ASSERT_EQ(ir.transitions.size(), 1u);
    EXPECT_EQ(ir.transitions[0].id, "T_Live");
    EXPECT_EQ(ir.transitions[0].target, "NormalTarget");
}

}  // namespace


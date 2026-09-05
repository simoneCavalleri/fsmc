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
 * @brief Test Intent: Verify LTL formula tokenization and operator parsing (G, F, X, !, &&, ||, U, ->).
 *
 * Scenario:
 * - Parse unary temporal operators (Globally, Finally, Next, Not).
 * - Parse binary temporal operators (And, Or, Until, Implies).
 */
TEST(LtlParserTest, BasicUnaryAndBinaryOperators) {
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
 * @brief Test Intent: Verify complex nested temporal logic formulas (response properties, mutual exclusion).
 *
 * Scenario:
 * - Parse `G (LowBattery -> F SafeLand)`.
 * - Parse `G (! (StateA && StateB))`.
 */
TEST(LtlParserTest, ComplexTemporalFormulas) {
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
 * @brief Test Intent: Verify `@fsm:property` and `@fsm:var` directive parsing.
 *
 * Scenario:
 * - Parse formal property directive string with LTL specification and traceability requirement ID.
 * - Parse variable directive string with domain bounds min/max.
 */
TEST(DirectiveParserTest, PropertyAndVariableDirectives) {
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
 * @brief Test Intent: Verify safety invariant evaluation, violation detection, and step-by-step trace generation.
 *
 * Scenario:
 * - Verify invariant `G (! (Idle && Armed))` passes.
 * - Verify safety property `G (! HazardFault)` fails, generating a 3-step counterexample trace (Idle -> Arming ->
 * HazardFault).
 */
TEST(ModelCheckerTest, SafetyInvariantPassedAndViolated) {
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
 * @brief Test Intent: Verify response liveness property verification (`G (Trigger -> F Target)`).
 *
 * Scenario:
 * - State machine moves Standby -> InFlight -> ReturnToHome -> Landed.
 * - Verify property `G (InFlight -> F Landed)` passes.
 */
TEST(ModelCheckerTest, ResponseLivenessVerification) {
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
 * @brief Test Intent: Verify integration of formal verification within PassManager optimization pipeline.
 *
 * Scenario:
 * - Run PassManager default pipeline over an FSM with liveness properties.
 * - Verify pipeline execution succeeds with 0 diagnostic errors.
 */
TEST(ModelCheckerTest, PassManagerIntegration) {
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
 * @brief Test Intent: Verify nuXmv / SMV formal model generation with state transitions and LTLSPEC.
 *
 * Scenario:
 * - Serialize FsmIr with state variables and properties into SMV format.
 * - Verify `MODULE main`, state domain, variable domains, init state, and `LTLSPEC` clauses are emitted.
 */
TEST(SmvSerializerTest, GenerateValidSmvModule) {
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
 * @brief Test Intent: Verify counterexample trace generation when reaching a prohibited fatal error state.
 *
 * Scenario:
 * - FSM reaches FatalError on Fault trigger.
 * - Property asserts `G (!FatalError)`.
 * - Verify counterexample trace accurately begins in Idle and concludes in FatalError.
 */
TEST(ModelCheckerTest, CounterexampleTraceGenerationOnViolation) {
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
 * @brief Test Intent: Verify SMV serializer emission for CTLSPEC temporal properties.
 *
 * Scenario:
 * - Verify SMV serializer formats CTL properties and state declarations accurately.
 */
TEST(SmvSerializerTest, GenerateCtlSpecProperties) {
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

}  // namespace

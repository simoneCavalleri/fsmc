/**
 * @file test_ltl_model_checker_extended.cpp
 * @brief Unit tests for extended LTL model checking operators and EFSM interval integration.
 *
 * Validates:
 * - Strong Until (P U Q) backward fixed-point verification and counterexample synthesis.
 * - Next operator (X P) and Next-Response (G (P -> X Q)).
 * - Infinitely Often / Recurrence (G F P) via Tarjan's strongly connected components.
 * - Eventually Always / Persistence (F G P) via SCC analysis.
 * - EFSM Abstract Interpretation integration with temporal logic model checking.
 */

#include <gtest/gtest.h>

#include "fsm/frontend/directive/ltl_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"
#include "fsm/middleend/analysis/model_checker.hpp"

namespace {

using namespace fsm::ir;
using namespace fsm::frontend::directive;
using namespace fsm::middleend::analysis;

/**
 * @brief Helper to construct a simple linear 3-state model: A -> B -> C
 */
FsmIr make_linear_model() {
    FsmIr ir;
    ir.name = "LinearModel";
    ir.initial_state = "A";
    ir.add_state("A");
    ir.add_state("B");
    ir.add_state("C");
    ir.add_transition("A", "B", SignalTrigger{"step1", ""});
    ir.add_transition("B", "C", SignalTrigger{"step2", ""});
    return ir;
}

/**
 * @brief Helper to construct a cyclic model: A -> B -> C -> B (cycle between B and C)
 */
FsmIr make_cyclic_model() {
    FsmIr ir;
    ir.name = "CyclicModel";
    ir.initial_state = "A";
    ir.add_state("A");
    ir.add_state("B");
    ir.add_state("C");
    ir.add_transition("A", "B", SignalTrigger{"start", ""});
    ir.add_transition("B", "C", SignalTrigger{"tick", ""});
    ir.add_transition("C", "B", SignalTrigger{"tock", ""});
    return ir;
}

// ============================================================================
// 1. Strong Until Operator: P U Q
// ============================================================================

TEST(ExtendedLtlModelChecker, StrongUntil_SatisfiedOnReachablePath_Passes) {
    // Model: A -> B -> C
    // Property: (state == A || state == B) U (state == C)
    // Starting in A, condition holds at A, then B, until C satisfies Q.
    auto ir = make_linear_model();
    ModelChecker checker(ir);

    FormalProperty prop("A_Until_C", PropertyKind::Safety, "(state == A || state == B) U (state == C)");
    prop.ast = LtlPropertyParser::parse("(state == A || state == B) U (state == C)");

    auto res = checker.verify_property(prop);
    EXPECT_TRUE(res.passed);
    EXPECT_TRUE(res.counterexample_trace.empty());
}

TEST(ExtendedLtlModelChecker, StrongUntil_ViolatingPBeforeQ_FailsWithCounterexample) {
    // Model: A -> B -> C
    // Property: (state == A) U (state == C)
    // In state B, neither Q (state == C) nor P (state == A) holds!
    auto ir = make_linear_model();
    ModelChecker checker(ir);

    FormalProperty prop("PrematureViolation", PropertyKind::Safety, "(state == A) U (state == C)");
    prop.ast = LtlPropertyParser::parse("(state == A) U (state == C)");

    auto res = checker.verify_property(prop);
    EXPECT_FALSE(res.passed);
    EXPECT_FALSE(res.counterexample_trace.empty());
}

TEST(ExtendedLtlModelChecker, StrongUntil_TargetQUnreachableInDeadEnd_Fails) {
    // Model: A -> B (no transition to C)
    FsmIr ir;
    ir.name = "DeadEndModel";
    ir.initial_state = "A";
    ir.add_state("A");
    ir.add_state("B");
    ir.add_state("C");
    ir.add_transition("A", "B", SignalTrigger{"step", ""});

    ModelChecker checker(ir);
    FormalProperty prop("UnreachableQ", PropertyKind::Safety, "(state == A || state == B) U (state == C)");
    prop.ast = LtlPropertyParser::parse("(state == A || state == B) U (state == C)");

    auto res = checker.verify_property(prop);
    EXPECT_FALSE(res.passed);
    EXPECT_FALSE(res.counterexample_trace.empty());
}

// ============================================================================
// 2. Next Operator: X P and G (P -> X Q)
// ============================================================================

TEST(ExtendedLtlModelChecker, NextOperator_SuccessorsSatisfyPredicate_Passes) {
    // Model: A -> B and A -> C
    // All immediate successors of root state A satisfy (state == B || state == C)
    FsmIr ir;
    ir.name = "BranchingModel";
    ir.initial_state = "A";
    ir.add_state("A");
    ir.add_state("B");
    ir.add_state("C");
    ir.add_transition("A", "B", SignalTrigger{"go_b", ""});
    ir.add_transition("A", "C", SignalTrigger{"go_c", ""});

    ModelChecker checker(ir);
    FormalProperty prop("NextStep", PropertyKind::Safety, "X (state == B || state == C)");
    prop.ast = LtlPropertyParser::parse("X (state == B || state == C)");

    auto res = checker.verify_property(prop);
    EXPECT_TRUE(res.passed);
    EXPECT_TRUE(res.counterexample_trace.empty());
}

TEST(ExtendedLtlModelChecker, NextOperator_SuccessorViolatesPredicate_FailsWithCounterexample) {
    // Model: A -> B and A -> C
    // Property: X (state == B)  (fails because C is also an immediate successor)
    FsmIr ir;
    ir.name = "BranchingModel";
    ir.initial_state = "A";
    ir.add_state("A");
    ir.add_state("B");
    ir.add_state("C");
    ir.add_transition("A", "B", SignalTrigger{"go_b", ""});
    ir.add_transition("A", "C", SignalTrigger{"go_c", ""});

    ModelChecker checker(ir);
    FormalProperty prop("NextOnlyB", PropertyKind::Safety, "X (state == B)");
    prop.ast = LtlPropertyParser::parse("X (state == B)");

    auto res = checker.verify_property(prop);
    EXPECT_FALSE(res.passed);
    EXPECT_FALSE(res.counterexample_trace.empty());
    // Trace should demonstrate reaching C
    EXPECT_EQ(res.counterexample_trace.back().state_name, "C");
}

TEST(ExtendedLtlModelChecker, NextResponse_GloballyImpliesNext_VerifiesStepObligation) {
    // Model: A -> B -> C -> B
    // Property: G (state == B -> X (state == C))
    // Whenever B is active, every outgoing step must lead to C.
    auto ir = make_cyclic_model();
    ModelChecker checker(ir);

    FormalProperty prop("B_Next_C", PropertyKind::Liveness, "G (state == B -> X (state == C))");
    prop.ast = LtlPropertyParser::parse("G (state == B -> X (state == C))");

    auto res = checker.verify_property(prop);
    EXPECT_TRUE(res.passed);
}

TEST(ExtendedLtlModelChecker, NextResponse_ViolatedStep_FailsWithTrace) {
    // Model: A -> B -> C -> B, plus B -> D
    auto ir = make_cyclic_model();
    ir.add_state("D");
    ir.add_transition("B", "D", SignalTrigger{"divert", ""});

    ModelChecker checker(ir);
    FormalProperty prop("B_Next_C_Strict", PropertyKind::Liveness, "G (state == B -> X (state == C))");
    prop.ast = LtlPropertyParser::parse("G (state == B -> X (state == C))");

    auto res = checker.verify_property(prop);
    EXPECT_FALSE(res.passed);
    EXPECT_FALSE(res.counterexample_trace.empty());
}

// ============================================================================
// 3. Recurrence / Infinitely Often: G F P
// ============================================================================

TEST(ExtendedLtlModelChecker, InfinitelyOften_CycleContainsTargetState_Passes) {
    // Model: A -> B -> C -> B (cycle B <-> C)
    // Property: G F (state == C)
    // The cycle contains state C, so C is visited infinitely often on the cycle.
    auto ir = make_cyclic_model();
    ModelChecker checker(ir);

    FormalProperty prop("Recurrence_C", PropertyKind::Liveness, "G F (state == C)");
    prop.ast = LtlPropertyParser::parse("G F (state == C)");

    auto res = checker.verify_property(prop);
    EXPECT_TRUE(res.passed);
}

TEST(ExtendedLtlModelChecker, InfinitelyOften_CycleWithoutTargetState_FailsWithTrace) {
    // Model: A -> B -> C -> B (cycle between B and C)
    // Property: G F (state == A)
    // Once execution enters {B, C}, state A is never visited again!
    auto ir = make_cyclic_model();
    ModelChecker checker(ir);

    FormalProperty prop("Recurrence_A", PropertyKind::Liveness, "G F (state == A)");
    prop.ast = LtlPropertyParser::parse("G F (state == A)");

    auto res = checker.verify_property(prop);
    EXPECT_FALSE(res.passed);
    EXPECT_FALSE(res.counterexample_trace.empty());
}

// ============================================================================
// 4. Persistence / Eventually Always: F G P
// ============================================================================

TEST(ExtendedLtlModelChecker, Persistence_EntersPermanentHoldingState_Passes) {
    // Model: Init -> Active -> Stable -> Stable (self loop at Stable)
    FsmIr ir;
    ir.name = "StabilizationModel";
    ir.initial_state = "Init";
    ir.add_state("Init");
    ir.add_state("Active");
    ir.add_state("Stable");
    ir.add_transition("Init", "Active", SignalTrigger{"start", ""});
    ir.add_transition("Active", "Stable", SignalTrigger{"converge", ""});
    ir.add_transition("Stable", "Stable", SignalTrigger{"maintain", ""});

    ModelChecker checker(ir);
    // Eventually the system settles permanently into Stable
    FormalProperty prop("Settle_Stable", PropertyKind::Liveness, "F G (state == Stable)");
    prop.ast = LtlPropertyParser::parse("F G (state == Stable)");

    auto res = checker.verify_property(prop);
    EXPECT_TRUE(res.passed);
}

TEST(ExtendedLtlModelChecker, Persistence_CycleOscillatesOutOfTarget_Fails) {
    // Model: A -> B -> C -> B (oscillates between B and C forever)
    // Property: F G (state == B)
    // Because C is also visited in the terminal cycle, state == B does not hold *always* in the future.
    auto ir = make_cyclic_model();
    ModelChecker checker(ir);

    FormalProperty prop("Persist_B", PropertyKind::Liveness, "F G (state == B)");
    prop.ast = LtlPropertyParser::parse("F G (state == B)");

    auto res = checker.verify_property(prop);
    EXPECT_FALSE(res.passed);
    EXPECT_FALSE(res.counterexample_trace.empty());
}

// ============================================================================
// 5. EFSM Interval Analysis Integration with Temporal Invariants
// ============================================================================

TEST(ExtendedLtlModelChecker, EFSMIntervalAnalysis_ComputesPerStateNumericBounds) {
    // Model with counter variable incremented along transitions:
    // Idle (cnt = 0) --[cnt := cnt + 5]--> Step1 --[cnt := cnt + 3]--> Step2
    FsmIr ir;
    ir.name = "EfsmCounterModel";
    ir.initial_state = "Idle";
    ir.add_state("Idle");
    ir.add_state("Step1");
    ir.add_state("Step2");

    VariableDefinition var_cnt("cnt", "uint32", "0", 0, 100, "Counter");
    ir.add_variable(var_cnt);

    TransitionEdge t1;
    t1.source = "Idle";
    t1.target = "Step1";
    t1.event = "go1";
    t1.transition_action = ActionSignature("act1", "cnt = cnt + 5;");
    t1.transition_action->assignments.push_back({"cnt", "cnt + 5"});
    ir.transitions.push_back(t1);

    TransitionEdge t2;
    t2.source = "Step1";
    t2.target = "Step2";
    t2.event = "go2";
    t2.transition_action = ActionSignature("act2", "cnt = cnt + 3;");
    t2.transition_action->assignments.push_back({"cnt", "cnt + 3"});
    ir.transitions.push_back(t2);

    EFSMIntervalAnalyzer analyzer(ir);
    auto intervals = analyzer.compute_state_intervals();

    ASSERT_TRUE(intervals.count("Idle") > 0);
    ASSERT_TRUE(intervals.count("Step1") > 0);
    ASSERT_TRUE(intervals.count("Step2") > 0);

    EXPECT_DOUBLE_EQ(intervals["Idle"]["cnt"].lo, 0.0);
    EXPECT_DOUBLE_EQ(intervals["Idle"]["cnt"].hi, 0.0);

    EXPECT_DOUBLE_EQ(intervals["Step1"]["cnt"].lo, 5.0);
    EXPECT_DOUBLE_EQ(intervals["Step1"]["cnt"].hi, 5.0);

    EXPECT_DOUBLE_EQ(intervals["Step2"]["cnt"].lo, 8.0);
    EXPECT_DOUBLE_EQ(intervals["Step2"]["cnt"].hi, 8.0);

    // Now verify through ModelChecker that G (cnt <= 10) holds across all reachable states
    ModelChecker checker(ir);
    FormalProperty prop_bounded("CntBounded", PropertyKind::Safety, "G (cnt <= 10)");
    prop_bounded.ast = LtlPropertyParser::parse("G (cnt <= 10)");

    auto res_bounded = checker.verify_property(prop_bounded);
    EXPECT_TRUE(res_bounded.passed);

    // Verify that G (cnt < 7) fails because in Step2 cnt == 8
    FormalProperty prop_tight("CntTight", PropertyKind::Safety, "G (cnt < 7)");
    prop_tight.ast = LtlPropertyParser::parse("G (cnt < 7)");

    auto res_tight = checker.verify_property(prop_tight);
    EXPECT_FALSE(res_tight.passed);
    EXPECT_FALSE(res_tight.counterexample_trace.empty());
}

}  // namespace

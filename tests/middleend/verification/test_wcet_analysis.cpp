/**
 * @file test_wcet_analysis.cpp
 * @brief Unit tests for WcetAnalysisPass static execution time bounding and Zeno cycle detection.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/passes/wcet_analysis_pass.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend::passes;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify WcetAnalysisPass detects infinite zero-time Zeno cycles.
 * @scenario Cyclic eventless transitions between states LoopA and LoopB.
 * @expected Pass returns false, flags Zeno cycle presence, and reports diagnostic error E_ZENO_CYCLE.
 */
TEST(WcetAnalysis, EventlessCyclicTransitions_EmitsZenoCycleError) {
    FsmIr model;
    model.name = "ZenoMachine";
    model.add_state("LoopA");
    model.add_state("LoopB");

    TransitionEdge t1;
    t1.source = "LoopA";
    t1.target = "LoopB";
    t1.event = "";  // immediate eventless
    model.add_transition(t1);

    TransitionEdge t2;
    t2.source = "LoopB";
    t2.target = "LoopA";
    t2.event = "";  // immediate eventless
    model.add_transition(t2);

    WcetAnalysisPass pass;
    DiagnosticEngine diag;
    bool ok = pass.run(model, diag);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(pass.has_zeno_cycle());
    EXPECT_TRUE(diag.has_errors());
}

/**
 * @brief Verify WcetAnalysisPass computes bounded micro-steps for terminating chains.
 * @scenario Linear sequence of completion transitions: Step1 -> Step2 -> Step3 -> Quiescent.
 * @expected Pass completes successfully with no Zeno cycle and computes exactly 3 maximum micro-steps.
 */
TEST(WcetAnalysis, LinearEventlessChains_ComputesBoundedMicroSteps) {
    FsmIr model;
    model.name = "ChainedStepMachine";
    model.add_state("Step1");
    model.add_state("Step2");
    model.add_state("Step3");
    model.add_state("Quiescent");

    TransitionEdge t1;
    t1.source = "Step1";
    t1.target = "Step2";
    t1.event = "completion_event";
    model.add_transition(t1);

    TransitionEdge t2;
    t2.source = "Step2";
    t2.target = "Step3";
    t2.event = "completion_event";
    model.add_transition(t2);

    TransitionEdge t3;
    t3.source = "Step3";
    t3.target = "Quiescent";
    t3.event = "completion_event";
    model.add_transition(t3);

    WcetAnalysisPass pass;
    DiagnosticEngine diag;
    bool ok = pass.run(model, diag);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(pass.has_zeno_cycle());
    EXPECT_EQ(pass.max_micro_steps(), 3u);
}

}  // namespace

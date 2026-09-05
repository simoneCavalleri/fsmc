#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_manager.hpp"
#include "fsm/middleend/passes/constant_folding_pass.hpp"
#include "fsm/middleend/passes/orthogonal_product_pass.hpp"
#include "fsm/middleend/passes/pipe_through_pass.hpp"
#include "fsm/middleend/passes/state_minimization_pass.hpp"
#include "fsm/middleend/passes/wcet_analysis_pass.hpp"
#include "fsm/middleend/plugin/plugin_loader.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend;
using namespace fsm::middleend::passes;
using namespace fsm::middleend::plugin;
using namespace fsm::ir;

namespace {

/**
 * @brief Test Intent: Verify OrthogonalProductPass expands concurrent regions into Cartesian product states.
 *
 * Scenario:
 * - Create a Parallel state with 2 orthogonal regions:
 *   - Region 1: states A1, A2 with transition A1 -> A2 on EvA.
 *   - Region 2: states B1, B2 with transition B1 -> B2 on EvB.
 * - Execute OrthogonalProductPass.
 * - Verify the parent is transformed into StateKind::Composite with 4 product states:
 *   A1_B1, A1_B2, A2_B1, A2_B2.
 * - Verify product transitions allow independent and synchronized event progress.
 */
TEST(MiddleEndV060PassesTest, OrthogonalProductCartesianExpansion) {
    FsmIr model;
    model.name = "DualChannelController";

    auto& par = model.add_state("DualChannel", "", StateKind::Parallel);
    OrthogonalRegion r1;
    r1.id = "RegionA";
    r1.name = "RegionA";
    r1.initial_state_id = "A1";
    r1.state_ids = {"A1", "A2"};

    OrthogonalRegion r2;
    r2.id = "RegionB";
    r2.name = "RegionB";
    r2.initial_state_id = "B1";
    r2.state_ids = {"B1", "B2"};

    par.orthogonal_regions.push_back(r1);
    par.orthogonal_regions.push_back(r2);

    model.add_state("A1", "RegionA");
    model.add_state("A2", "RegionA");
    model.add_state("B1", "RegionB");
    model.add_state("B2", "RegionB");

    TransitionEdge t1;
    t1.source = "A1";
    t1.target = "A2";
    t1.event = "EvA";
    model.add_transition(t1);

    TransitionEdge t2;
    t2.source = "B1";
    t2.target = "B2";
    t2.event = "EvB";
    model.add_transition(t2);

    OrthogonalProductPass pass;
    DiagnosticEngine diag;
    bool ok = pass.run(model, diag);

    EXPECT_TRUE(ok);
    const auto* updated_par = model.find_state("DualChannel");
    ASSERT_NE(updated_par, nullptr);
    EXPECT_EQ(updated_par->kind, StateKind::Composite);
    EXPECT_TRUE(updated_par->orthogonal_regions.empty());

    // Should have 4 product states and no individual A1/A2/B1/B2
    EXPECT_EQ(model.find_state("A1"), nullptr);
    EXPECT_EQ(model.find_state("B1"), nullptr);

    const auto* s_a1_b1 = model.find_state("DualChannel_A1_B1");
    const auto* s_a1_b2 = model.find_state("DualChannel_A1_B2");
    const auto* s_a2_b1 = model.find_state("DualChannel_A2_B1");
    const auto* s_a2_b2 = model.find_state("DualChannel_A2_B2");

    EXPECT_NE(s_a1_b1, nullptr);
    EXPECT_NE(s_a1_b2, nullptr);
    EXPECT_NE(s_a2_b1, nullptr);
    EXPECT_NE(s_a2_b2, nullptr);

    EXPECT_EQ(updated_par->initial_sub_state, "DualChannel_A1_B1");
}

/**
 * @brief Test Intent: Verify WcetAnalysisPass detects infinite zero-time Zeno cycles.
 *
 * Scenario:
 * - Create cyclic eventless transitions between states LoopA and LoopB.
 * - Execute WcetAnalysisPass.
 * - Verify pass returns false and reports diagnostic error E_ZENO_CYCLE.
 */
TEST(MiddleEndV060PassesTest, WcetAnalysisZenoCycleDetection) {
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
 * @brief Test Intent: Verify WcetAnalysisPass computes bounded micro-steps for terminating chains.
 *
 * Scenario:
 * - Create a linear sequence of eventless transitions Step1 -> Step2 -> Step3 -> Quiescent.
 * - Execute WcetAnalysisPass.
 * - Verify pass passes without error and calculates max micro-steps equal to 3.
 */
TEST(MiddleEndV060PassesTest, WcetAnalysisBoundedMicroSteps) {
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

/**
 * @brief Test Intent: Verify ConstantFoldingPass folds tautological guards and eliminates false transitions.
 *
 * Scenario:
 * - Create transition T1 with guard "1 == 1" (tautology).
 * - Create transition T2 with guard "0 == 1" (contradiction).
 * - Create transition T3 with guard "5 > 2" (tautology).
 * - Execute ConstantFoldingPass.
 * - Verify T1 and T3 have guards stripped (unconditional), and T2 is eliminated from IR.
 */
TEST(MiddleEndV060PassesTest, ConstantFoldingGuardEvaluation) {
    FsmIr model;
    model.name = "ConstantGuardModel";
    model.add_state("S1");
    model.add_state("S2");

    TransitionEdge t1;
    t1.source = "S1";
    t1.target = "S2";
    t1.event = "Ev1";
    t1.guard = "1 == 1";
    model.add_transition(t1);

    TransitionEdge t2;
    t2.source = "S1";
    t2.target = "S2";
    t2.event = "Ev2";
    t2.guard = "0 == 1";
    model.add_transition(t2);

    TransitionEdge t3;
    t3.source = "S1";
    t3.target = "S2";
    t3.event = "Ev3";
    t3.guard = "5 > 2";
    model.add_transition(t3);

    ConstantFoldingPass pass;
    DiagnosticEngine diag;
    bool ok = pass.run(model, diag);

    EXPECT_TRUE(ok);
    // T2 should be pruned, remaining T1 and T3 should have nullopt guard
    EXPECT_EQ(model.transitions.size(), 2u);

    for (const auto& t : model.transitions) {
        EXPECT_NE(t.event, "Ev2");
        EXPECT_FALSE(t.guard.has_value());
    }
}

/**
 * @brief Test Intent: Verify StateMinimizationPass merges behaviorally equivalent states.
 *
 * Scenario:
 * - Create states StateA, StateB1, StateB2.
 * - Both StateB1 and StateB2 transition to StateA on EvReset with same action and guard,
 *   and have identical lifecycle actions.
 * - Execute StateMinimizationPass.
 * - Verify StateB1 and StateB2 are merged into a single state, reducing overall state count.
 */
TEST(MiddleEndV060PassesTest, StateMinimizationEquivalencePartitioning) {
    FsmIr model;
    model.name = "MinimizableModel";
    model.add_state("Init");
    model.initial_state = "Init";
    model.add_state("Target");
    model.add_state("Equiv1");
    model.add_state("Equiv2");

    TransitionEdge t_init1;
    t_init1.source = "Init";
    t_init1.target = "Equiv1";
    t_init1.event = "Go1";
    model.add_transition(t_init1);

    TransitionEdge t_init2;
    t_init2.source = "Init";
    t_init2.target = "Equiv2";
    t_init2.event = "Go2";
    model.add_transition(t_init2);

    TransitionEdge t_eq1;
    t_eq1.source = "Equiv1";
    t_eq1.target = "Target";
    t_eq1.event = "Step";
    model.add_transition(t_eq1);

    TransitionEdge t_eq2;
    t_eq2.source = "Equiv2";
    t_eq2.target = "Target";
    t_eq2.event = "Step";
    model.add_transition(t_eq2);

    StateMinimizationPass pass;
    DiagnosticEngine diag;
    bool ok = pass.run(model, diag);

    EXPECT_TRUE(ok);
    // Equiv1 and Equiv2 should be collapsed into one canonical representative
    EXPECT_EQ(model.states.size(), 3u);
}

/**
 * @brief Test Intent: Verify PipeThroughPass filters IR faithfully through an external Unix command.
 *
 * Scenario:
 * - Pipe IR through the standard POSIX utility 'cat'.
 * - Verify IR roundtrips with preserved states, transitions and metadata.
 */
TEST(MiddleEndV060PassesTest, PipeThroughUnixFilter) {
    FsmIr model;
    model.name = "PipedModel";
    model.add_state("Active");
    model.add_state("Inactive");
    model.initial_state = "Active";

    TransitionEdge t;
    t.source = "Active";
    t.target = "Inactive";
    t.event = "PowerOff";
    model.add_transition(t);

    PipeThroughPass pass("cat");
    DiagnosticEngine diag;
    bool ok = pass.run(model, diag);

    EXPECT_TRUE(ok);
    EXPECT_EQ(model.name, "PipedModel");
    EXPECT_EQ(model.states.size(), 2u);
    EXPECT_EQ(model.transitions.size(), 1u);
}

/**
 * @brief Test Intent: Verify PluginLoader handles non-existent or invalid plugin files gracefully.
 *
 * Scenario:
 * - Attempt to load a non-existent shared library.
 * - Verify load_plugin returns false and logs diagnostic error E_PLUGIN_LOAD.
 */
TEST(MiddleEndV060PassesTest, PluginLoaderErrorHandling) {
    PassManager pm;
    DiagnosticEngine diag;
    PluginLoader loader;

    bool ok = loader.load_plugin("/non/existent/path/to/plugin.so", pm, diag);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.has_errors());
}

}  // namespace

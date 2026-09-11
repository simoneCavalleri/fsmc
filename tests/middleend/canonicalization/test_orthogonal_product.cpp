/**
 * @file test_orthogonal_product.cpp
 * @brief Unit tests for OrthogonalProductPass Cartesian product canonicalization.
 */

#include <gtest/gtest.h>

#include <unordered_set>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/passes/fork_join_lowering_pass.hpp"
#include "fsm/middleend/passes/orthogonal_product_pass.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend::passes;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify OrthogonalProductPass expands concurrent regions into Cartesian product states.
 * @scenario Parallel state 'DualChannel' with 2 orthogonal regions (A with A1, A2; B with B1, B2).
 * @expected Composite state with 4 Cartesian product states (A1_B1, A1_B2, A2_B1, A2_B2) and synchronized transitions.
 */
TEST(OrthogonalProduct, ConcurrentRegions_ExpandedToCartesianProductStates) {
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
 * @brief Reject malformed parallel states instead of silently changing their semantics.
 * @scenario Run OrthogonalProductPass on a parallel state with only one region.
 * @expected The pass reports EORTHO001, leaves the state parallel, and performs no lowering.
 */
TEST(OrthogonalProduct, IncompleteParallelState_ReportsDiagnosticWithoutMutation) {
    FsmIr model;
    model.name = "IncompleteParallelController";
    model.add_state("ParallelRoot", "", StateKind::Parallel);

    OrthogonalRegion only_region;
    only_region.id = "OnlyRegion";
    only_region.name = "OnlyRegion";
    only_region.initial_state_id = "Idle";
    only_region.state_ids = {"Idle"};
    model.find_state_mut("ParallelRoot")->orthogonal_regions.push_back(only_region);
    model.add_state("Idle", "OnlyRegion");

    OrthogonalProductPass pass;
    DiagnosticEngine diagnostics;
    EXPECT_FALSE(pass.run(model, diagnostics));
    ASSERT_EQ(diagnostics.get_diagnostics().size(), 1u);
    EXPECT_EQ(diagnostics.get_diagnostics().front().code, "EORTHO001");
    ASSERT_NE(model.find_state("ParallelRoot"), nullptr);
    EXPECT_EQ(model.find_state("ParallelRoot")->kind, StateKind::Parallel);
}

/**
 * @brief Preserve and update the parallel parent after vector-backed state replacement.
 * @scenario Lower two orthogonal regions while product states are erased and inserted in the state vector.
 * @expected The original parent remains addressable as a composite with the generated initial product state.
 */
TEST(OrthogonalProduct, ParentState_RemainsValidAfterProductReplacement) {
    FsmIr model;
    model.name = "StableParentController";
    auto& parent = model.add_state("ParallelRoot", "", StateKind::Parallel);

    OrthogonalRegion first;
    first.id = "First";
    first.name = "First";
    first.initial_state_id = "FirstIdle";
    first.state_ids = {"FirstIdle"};
    OrthogonalRegion second;
    second.id = "Second";
    second.name = "Second";
    second.initial_state_id = "SecondIdle";
    second.state_ids = {"SecondIdle"};
    parent.orthogonal_regions = {first, second};
    model.add_state("FirstIdle", "First");
    model.add_state("SecondIdle", "Second");

    OrthogonalProductPass pass;
    DiagnosticEngine diagnostics;
    ASSERT_TRUE(pass.run(model, diagnostics));
    const auto* updated_parent = model.find_state("ParallelRoot");
    ASSERT_NE(updated_parent, nullptr);
    EXPECT_EQ(updated_parent->kind, StateKind::Composite);
    EXPECT_EQ(updated_parent->initial_sub_state, "ParallelRoot_FirstIdle_SecondIdle");
}

/**
 * @brief Regression test: no zombie transitions survive the purge step.
 *
 * @scenario A parallel state 'P' has two regions (R1: {X, Y}, R2: {A, B}).
 *   An intra-region transition exists: X --Ev--> Operational_Y_A (i.e. the
 *   target has been remapped by step 5 before the purge).  With the old "&&"
 *   condition the purge missed this transition because its target was no longer
 *   in all_sub_state_names, leaving a zombie transition that caused the emitter
 *   to reference the undeclared type 'X'.
 *
 * @expected After lowering, no transition whose source is one of the original
 *   sub-states (X, Y, A, B) remains in the IR.
 */
TEST(OrthogonalProduct, ZombieTransitions_AreFullyPurgedAfterTargetRemap) {
    FsmIr model;
    model.name = "ZombieController";

    // Parallel parent with two regions
    auto& par = model.add_state("P", "", StateKind::Parallel);

    OrthogonalRegion r1;
    r1.id = "R1";
    r1.name = "R1";
    r1.initial_state_id = "X";
    r1.state_ids = {"X", "Y"};

    OrthogonalRegion r2;
    r2.id = "R2";
    r2.name = "R2";
    r2.initial_state_id = "A";
    r2.state_ids = {"A", "B"};

    par.orthogonal_regions = {r1, r2};

    model.add_state("X", "R1");
    model.add_state("Y", "R1");
    model.add_state("A", "R2");
    model.add_state("B", "R2");

    // Intra-region transition: X --Ev1--> Y (within R1)
    TransitionEdge t1;
    t1.source = "X";
    t1.target = "Y";
    t1.event = "Ev1";
    model.add_transition(t1);

    // Intra-region transition: A --Ev2--> B (within R2)
    TransitionEdge t2;
    t2.source = "A";
    t2.target = "B";
    t2.event = "Ev2";
    model.add_transition(t2);

    OrthogonalProductPass pass;
    DiagnosticEngine diag;
    ASSERT_TRUE(pass.run(model, diag));

    // No transition may have a pre-lowering sub-state as its source.
    // Before the fix, t1 and t2 could survive as zombies when the target had
    // been remapped (target no longer in all_sub_state_names → "&&" missed them).
    const std::unordered_set<std::string> pre_lowering_sources{"X", "Y", "A", "B"};
    for (const auto& t : model.transitions) {
        EXPECT_EQ(pre_lowering_sources.count(t.source), 0u)
            << "Zombie transition survived purge: " << t.source << " --" << t.event << "--> " << t.target;
    }

    // Sanity: original sub-states must not be present in model.states either.
    EXPECT_EQ(model.find_state("X"), nullptr);
    EXPECT_EQ(model.find_state("Y"), nullptr);
    EXPECT_EQ(model.find_state("A"), nullptr);
    EXPECT_EQ(model.find_state("B"), nullptr);

    // Product states must exist.
    EXPECT_NE(model.find_state("P_X_A"), nullptr);
    EXPECT_NE(model.find_state("P_X_B"), nullptr);
    EXPECT_NE(model.find_state("P_Y_A"), nullptr);
    EXPECT_NE(model.find_state("P_Y_B"), nullptr);
}

/**
 * @brief External transition targeting a sub-state is remapped to the matching first product state.
 *
 * @scenario An external state 'Idle' transitions into sub-state 'X' of a parallel region.
 *   After lowering, the target must be remapped to the first product state that contains 'X'.
 *
 * @expected The transition from 'Idle' now targets a product state, not the removed 'X'.
 */
TEST(OrthogonalProduct, ExternalTransition_TargetingSubState_RemappedToProductState) {
    FsmIr model;
    model.name = "ExternalRemapController";

    model.add_state("Idle");

    auto& par = model.add_state("P", "", StateKind::Parallel);

    OrthogonalRegion r1;
    r1.id = "R1";
    r1.name = "R1";
    r1.initial_state_id = "X";
    r1.state_ids = {"X", "Y"};

    OrthogonalRegion r2;
    r2.id = "R2";
    r2.name = "R2";
    r2.initial_state_id = "A";
    r2.state_ids = {"A"};

    par.orthogonal_regions = {r1, r2};
    model.add_state("X", "R1");
    model.add_state("Y", "R1");
    model.add_state("A", "R2");

    // External transition: Idle --Start--> X (sub-state of R1)
    TransitionEdge ext;
    ext.source = "Idle";
    ext.target = "X";
    ext.event = "Start";
    model.add_transition(ext);

    OrthogonalProductPass pass;
    DiagnosticEngine diag;
    ASSERT_TRUE(pass.run(model, diag));

    // The external transition must have been remapped to the first product
    // state that contains 'X', which is 'P_X_A'.
    bool found = false;
    for (const auto& t : model.transitions) {
        if (t.source == "Idle" && t.event == "Start") {
            EXPECT_EQ(t.target, "P_X_A") << "External transition was not remapped to the correct product state";
            found = true;
        }
    }
    EXPECT_TRUE(found) << "External transition from 'Idle' was lost after lowering";
}

/**
 * @brief Verify that a Fork entering orthogonal regions is resolved to the corresponding product state.
 * @scenario State 'Init' transitions via Fork1 to RegionA (A1) and RegionB (B1).
 * @expected ForkJoinLoweringPass synthesizes multi-target edge; OrthogonalProductPass resolves it
 *           to target the Cartesian product state 'DualChannel_A1_B1' directly with target_ids cleared.
 */
TEST(OrthogonalProduct, ForkToParallelRegions_ResolvedToProductState) {
    FsmIr model;
    model.name = "ForkProductModel";
    model.initial_state = "Init";
    model.add_state("Init");

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

    par.orthogonal_regions = {r1, r2};
    model.add_state("A1", "RegionA");
    model.add_state("A2", "RegionA");
    model.add_state("B1", "RegionB");
    model.add_state("B2", "RegionB");

    // Fork pseudostate
    StateNode fork_node("Fork1");
    fork_node.kind = StateKind::Fork;
    model.add_state(fork_node);

    model.add_transition(TransitionEdge("Init", "Fork1", "EvStart"));
    model.add_transition(TransitionEdge("Fork1", "A1", ""));
    model.add_transition(TransitionEdge("Fork1", "B1", ""));

    DiagnosticEngine diag;
    ASSERT_TRUE(ForkJoinLoweringPass::run(model, diag));
    EXPECT_EQ(model.find_state("Fork1"), nullptr);

    OrthogonalProductPass product_pass;
    ASSERT_TRUE(product_pass.run(model, diag));

    // The transition from 'Init' must now directly target the product state 'DualChannel_A1_B1'
    const TransitionEdge* fork_res = nullptr;
    for (const auto& t : model.transitions) {
        if (t.source == "Init" && t.event == "EvStart") {
            fork_res = &t;
            break;
        }
    }
    ASSERT_NE(fork_res, nullptr);
    EXPECT_EQ(fork_res->target, "DualChannel_A1_B1");
    EXPECT_TRUE(fork_res->target_ids.empty());
    EXPECT_TRUE(fork_res->multi_target_ids.empty());
}

/**
 * @brief Verify that a Join rendezvous exiting orthogonal regions is resolved from the corresponding product state.
 * @scenario A2 in RegionA and B2 in RegionB transition to Join1, which transitions to Done.
 * @expected ForkJoinLoweringPass synthesizes multi-source edge; OrthogonalProductPass resolves it
 *           to originate from the Cartesian product state 'DualChannel_A2_B2' directly with source_ids cleared.
 */
TEST(OrthogonalProduct, JoinFromParallelRegions_ResolvedFromProductState) {
    FsmIr model;
    model.name = "JoinProductModel";
    model.initial_state = "DualChannel";

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

    par.orthogonal_regions = {r1, r2};
    model.add_state("A1", "RegionA");
    model.add_state("A2", "RegionA");
    model.add_state("B1", "RegionB");
    model.add_state("B2", "RegionB");

    // Internal transitions to reach A2 and B2
    model.add_transition(TransitionEdge("A1", "A2", "EvStepA"));
    model.add_transition(TransitionEdge("B1", "B2", "EvStepB"));

    // Join pseudostate
    StateNode join_node("Join1");
    join_node.kind = StateKind::Join;
    model.add_state(join_node);

    model.add_state("Done");

    model.add_transition(TransitionEdge("A2", "Join1", ""));
    model.add_transition(TransitionEdge("B2", "Join1", ""));
    model.add_transition(TransitionEdge("Join1", "Done", "EvComplete"));

    DiagnosticEngine diag;
    ASSERT_TRUE(ForkJoinLoweringPass::run(model, diag));
    EXPECT_EQ(model.find_state("Join1"), nullptr);

    OrthogonalProductPass product_pass;
    ASSERT_TRUE(product_pass.run(model, diag));

    // The transition targeting 'Done' must now originate from 'DualChannel_A2_B2'
    const TransitionEdge* join_res = nullptr;
    for (const auto& t : model.transitions) {
        if (t.target == "Done" && t.event == "EvComplete") {
            join_res = &t;
            break;
        }
    }
    ASSERT_NE(join_res, nullptr);
    EXPECT_EQ(join_res->source, "DualChannel_A2_B2");
    EXPECT_TRUE(join_res->source_ids.empty());
    EXPECT_TRUE(join_res->multi_source_ids.empty());
}

/**
 * @brief Verify that exceeding the maximum configured product state limit aborts with EORTHO003.
 */
TEST(OrthogonalProduct, ProductExplosion_ExceedingLimit_ReportsDiagnosticAndAborts) {
    FsmIr model;
    model.name = "ExplosionModel";
    model.initial_state = "ParallelState";

    model.add_state("ParallelState", "", StateKind::Parallel);

    std::vector<OrthogonalRegion> regions;
    // Create 3 regions with 3 states each: 3 * 3 * 3 = 27 product states
    for (int r = 0; r < 3; ++r) {
        OrthogonalRegion region;
        region.id = "Region" + std::to_string(r);
        region.name = region.id;
        region.initial_state_id = region.id + "_S0";
        for (int s = 0; s < 3; ++s) {
            std::string s_id = region.id + "_S" + std::to_string(s);
            region.state_ids.push_back(s_id);
            model.add_state(s_id, region.id);
        }
        regions.push_back(region);
    }

    auto* par = model.find_state_mut("ParallelState");
    ASSERT_NE(par, nullptr);
    par->orthogonal_regions = regions;

    DiagnosticEngine diag;
    // Set limit to 20, whereas 27 states would be produced
    OrthogonalProductPass pass(/*max_product_states=*/20);
    EXPECT_FALSE(pass.run(model, diag));
    EXPECT_TRUE(diag.has_errors());

    bool found_eortho003 = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "EORTHO003") {
            found_eortho003 = true;
            break;
        }
    }
    EXPECT_TRUE(found_eortho003);
}

}  // namespace

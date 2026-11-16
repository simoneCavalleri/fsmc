/**
 * @file test_pass_manager.cpp
 * @brief Unit tests for PassManager execution pipeline, pass chaining, and statistical profiling.
 */

#include <gtest/gtest.h>

#include "fsm/middleend/pass_manager.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend;
using namespace fsm::middleend::passes;
using namespace fsm::middleend::analysis;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify PassManager default optimization and analysis pipeline execution.
 * @scenario Model containing unreachable trap state and choice pseudostate lacking fallback branch.
 * @expected Pipeline runs to completion, collects execution statistics, and emits diagnostic warnings W0201 and W0103.
 */
TEST(PassManager, DefaultPipelineExecution_CollectsStatisticsAndEmitsDiagnostics) {
    FsmIr ir;
    ir.name = "MissionFSM";
    ir.add_state("Standby");
    ir.add_state("Ascending", "InFlight");
    ir.add_state("InFlight", "", StateKind::Composite);
    ir.add_state("TrapState");  // Unreachable and deadlock trap
    ir.initial_state = "Standby";

    ir.add_choice_node("SpeedChoice");
    // Choice without fallback
    ir.add_transition("SpeedChoice", "Ascending", SignalTrigger{"Tick", "payload"}, GuardAstNode("HighRpm"));

    PassManager pm = PassManager::create_default_pipeline();
    DiagnosticEngine diag;

    bool success = pm.run(ir, diag);
    EXPECT_TRUE(success);  // Warnings do not fail execution unless fatal
    EXPECT_GE(pm.get_stats().size(), 3u);

    const auto& diagnostics = diag.get_diagnostics();
    EXPECT_GE(diagnostics.size(), 2u);

    bool found_trap = false;
    bool found_choice_warn = false;
    for (const auto& d : diagnostics) {
        if (d.code == "W0201" || d.code == "W0202")
            found_trap = true;
        if (d.code == "W0103")
            found_choice_warn = true;
    }
    EXPECT_TRUE(found_trap);
    EXPECT_TRUE(found_choice_warn);
}

/**
 * @brief Verify custom pass registration and extension in PassManager.
 * @scenario User creates CustomInstrumentationPass and registers it in a fresh PassManager.
 * @expected Pass is executed during pm.run, IR states receive injected metadata, and execution is logged in stats.
 */
TEST(PassManager, CustomPassRegistration_ModifiesIrAndRecordsExecutionStats) {
    class CustomInstrumentationPass : public IPass {
      public:
        [[nodiscard]] std::string name() const override { return "CustomInstrumentation"; }
        [[nodiscard]] std::string description() const override { return "Tags all states with trace metadata"; }

        bool run(FsmIr& ir, DiagnosticEngine& /*diag*/) override {
            for (auto& s : ir.states) {
                s.traceability_reqs.emplace_back("TRACE-AUTO-GEN");
            }
            return true;
        }
    };

    FsmIr ir;
    ir.add_state("Init");
    ir.add_state("Ready");

    PassManager pm;
    pm.add_pass(std::make_unique<CustomInstrumentationPass>());
    DiagnosticEngine diag;

    EXPECT_TRUE(pm.run(ir, diag));
    EXPECT_EQ(ir.states[0].traceability_reqs.size(), 1u);
    EXPECT_EQ(ir.states[0].traceability_reqs[0], "TRACE-AUTO-GEN");
    EXPECT_EQ(pm.get_stats().size(), 1u);
    EXPECT_EQ(pm.get_stats()[0].pass_name, "CustomInstrumentation");
}

TEST(PassManager, SampledChangeTriggerAndSubmachineWrappers_ExecuteCleanly) {
    FsmIr ir;
    ir.add_state("Idle");
    ir.add_state("Working");
    TransitionEdge t("Idle", "Working", "");
    t.trigger = ChangeTrigger("flag == true", true);
    ir.transitions.push_back(t);

    PassManager pm;
    pm.add_pass(std::make_unique<SampledChangeTriggerPassWrapper>());
    pm.add_pass(std::make_unique<SubmachineInliningPassWrapper>());

    DiagnosticEngine diag;
    EXPECT_TRUE(pm.run(ir, diag));
    EXPECT_EQ(pm.get_stats().size(), 2u);
    EXPECT_EQ(pm.get_stats()[0].pass_name, "SampledChangeTrigger");
    EXPECT_EQ(pm.get_stats()[1].pass_name, "SubmachineInlining");
    EXPECT_TRUE(std::holds_alternative<AnonymousTrigger>(ir.transitions[0].trigger));
}

/**
 * @brief Verify no-op transformation wrappers satisfy IPass::run() contract and analysis passes preserve IR.
 * @scenario Run lowering/optimization wrappers on a minimal IR with no matching constructs alongside analysis passes.
 * @expected Transformation wrappers return true (not fatal failure) and analysis stats mark modified_ir == false.
 */
TEST(PassManager, NoOpTransformationWrappersReturnSuccessAndAnalyticalPreserveIr) {
    FsmIr ir;
    ir.name = "MinimalPassManagerIr";
    ir.initial_state = "Idle";
    ir.add_state("Idle");

    PassManager pm;
    pm.add_pass(std::make_unique<SemanticValidationPass>());
    pm.add_pass(std::make_unique<HistoryLoweringPassWrapper>());
    pm.add_pass(std::make_unique<ForkJoinLoweringPassWrapper>());
    pm.add_pass(std::make_unique<DeferredEventLoweringPassWrapper>());
    pm.add_pass(std::make_unique<BoundaryActionFusionPassWrapper>());
    pm.add_pass(std::make_unique<OrthogonalProductPassWrapper>());
    pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
    pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
    pm.add_pass(std::make_unique<WcetAnalysisPassWrapper>());
    pm.add_pass(std::make_unique<EFSMDataPathPass>(false));

    DiagnosticEngine diag;
    EXPECT_TRUE(pm.run(ir, diag));
    ASSERT_EQ(pm.get_stats().size(), 10u);
    EXPECT_FALSE(pm.get_stats()[0].modified_ir);
    EXPECT_FALSE(pm.get_stats()[6].modified_ir);
    EXPECT_FALSE(pm.get_stats()[7].modified_ir);
    EXPECT_FALSE(pm.get_stats()[8].modified_ir);
    EXPECT_FALSE(pm.get_stats()[9].modified_ir);
}

/**
 * @brief Verify PassManager central registry instantiates all registered built-in passes.
 * @scenario Iterate over PassManager::registered_pass_names() and call create_pass_by_name().
 * @expected Every registered pass name resolves to a valid IPass instance, and unknown names return nullptr.
 */
TEST(PassManager, CentralPassRegistry_InstantiatesAllRegisteredPasses) {
    auto names = PassManager::registered_pass_names();
    EXPECT_GE(names.size(), 28u);
    for (std::string_view name : names) {
        auto pass = PassManager::create_pass_by_name(name, "cat");
        ASSERT_NE(pass, nullptr) << "Failed to instantiate registered pass: " << name;
        EXPECT_FALSE(pass->name().empty());
    }
    EXPECT_EQ(PassManager::create_pass_by_name("non-existent-pass-xyz"), nullptr);
}

/**
 * @brief Regression: 7-stage pipeline must preserve `deferred_events` for the native C++ runtime FIFO.
 * @scenario Run `PassManager::create_verified_7stage_pipeline(true)` on a model with `Busy` deferring `EvWork`.
 * @expected `Busy->deferred_events` still contains `EvWork` so the runtime can enqueue and recall it.
 */
TEST(PassManager, SevenStagePipeline_PreservesNativeDeferredEvents) {
    FsmIr ir;
    ir.name = "DeferredNative";
    ir.initial_state = "Busy";
    StateNode busy("Busy");
    busy.deferred_events.push_back("EvWork");
    ir.add_state(std::move(busy));
    ir.add_state("Ready");
    ir.add_state("Processing");
    ir.add_event("EvDone");
    ir.add_event("EvWork");
    ir.add_transition(TransitionEdge("Busy", "Ready", "EvDone"));
    ir.add_transition(TransitionEdge("Ready", "Processing", "EvWork"));

    auto pm = PassManager::create_verified_7stage_pipeline(true);
    DiagnosticEngine diag;
    ASSERT_TRUE(pm.run(ir, diag));

    const auto* busy_after = ir.find_state("Busy");
    ASSERT_NE(busy_after, nullptr);
    ASSERT_EQ(busy_after->deferred_events.size(), 1u);
    EXPECT_EQ(busy_after->deferred_events[0], "EvWork");
}

}  // namespace

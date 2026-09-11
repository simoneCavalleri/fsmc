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

}  // namespace

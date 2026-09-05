#include <gtest/gtest.h>

#include "fsm/middleend/pass_manager.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend;
using namespace fsm::middleend::passes;
using namespace fsm::middleend::analysis;
using namespace fsm::ir;

namespace {

/**
 * @brief Test Intent: Verify PassManager default optimization and analysis pipeline execution.
 *
 * Scenario:
 * - Run default optimization pipeline over an FSM containing an unreachable trap state and choice without fallback.
 * - Verify passes collect execution statistics and emit appropriate diagnostic warnings (W0201, W0103).
 */
TEST(PassManagerTest, RunDefaultPipeline) {
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
 * @brief Test Intent: Verify custom pass registration and extension in PassManager.
 *
 * Scenario:
 * - Create a custom `IPass` subclass (`CustomInstrumentationPass`).
 * - Register it on PassManager and run pipeline over FSM.
 * - Verify state metadata modification and pass statistics recording.
 */
TEST(PassManagerTest, CustomPassRegistration) {
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

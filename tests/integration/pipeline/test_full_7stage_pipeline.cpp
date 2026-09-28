/**
 * @file test_full_7stage_pipeline.cpp
 * @brief Integration tests for the verified 7-stage compilation pipeline, pass dependencies, and statistics.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_manager.hpp"

using namespace fsm;
using namespace fsm::ir;
using namespace fsm::middleend;
using namespace fsm::diagnostic;

namespace {

// Dummy pass requiring an unsatisfied prerequisite to verify pipeline order checking
class DummyRequiresPrereqPass : public IPass {
  public:
    [[nodiscard]] std::string name() const override { return "DummyRequiresPrereq"; }
    [[nodiscard]] std::string description() const override { return "Requires non-existent pass"; }
    [[nodiscard]] std::vector<std::string> required_prerequisites() const override { return {"NonExistentPass"}; }
    bool run(FsmIr& /*ir*/, DiagnosticEngine& /*diag*/) override { return true; }
};

/**
 * @brief Verify full execution of the verified 7-stage middle-end compilation pipeline.
 * @scenario Hierarchical mission system with history, deferred events, datapath variables, and preemption.
 * @expected Pipeline runs through all 7 stages with zero errors, annotates queue capacity, and collects metrics.
 */
TEST(VerifiedPipeline, MultiStageCompilerPipeline_LowersOptimizesAndVerifiesIr) {
    FsmIr ir;
    ir.name = "MissionSystem";
    ir.initial_state = "idle";
    ir.initial_state_id = "idle_id";
    ir.execution_semantics.preemption_priority = HierarchicalPriority::OuterFirst;

    ir.variables.emplace_back("battery_soc", DataType::int32(), "100");
    ir.variables.emplace_back("altitude", DataType::int32(), "0");

    StateNode idle("idle", "idle");
    idle.id = "idle_id";

    StateNode active("active", "active");
    active.id = "active_id";
    active.is_composite = true;
    active.has_history = true;
    active.initial_sub_state = "sub1";

    StateNode sub1("sub1", "sub1", "active");
    sub1.id = "sub1_id";
    sub1.parent_id = "active_id";

    StateNode sub2("sub2", "sub2", "active");
    sub2.id = "sub2_id";
    sub2.parent_id = "active_id";
    sub2.deferred_events = {"LAND"};

    active.children_ids = {"sub1_id", "sub2_id"};

    ir.states.push_back(idle);
    ir.states.push_back(active);
    ir.states.push_back(sub1);
    ir.states.push_back(sub2);

    // Transition idle -> sub1 on START
    TransitionEdge t1;
    t1.source = "idle";
    t1.target = "sub1";
    t1.event = "START";
    t1.source_id = "idle_id";
    t1.target_id = "sub1_id";
    t1.trigger = SignalTrigger("START");
    ActionSignature a1;
    StoreOp op1;
    op1.target = LValueTarget("battery_soc");
    op1.expression = "95";
    a1.instructions.emplace_back(op1);
    t1.transition_action = a1;
    ir.add_transition(t1);

    // Transition sub1 -> sub2 on CLIMB
    TransitionEdge t2;
    t2.source = "sub1";
    t2.target = "sub2";
    t2.event = "CLIMB";
    t2.source_id = "sub1_id";
    t2.target_id = "sub2_id";
    t2.trigger = SignalTrigger("CLIMB");
    t2.guard = "battery_soc > 20";
    t2.guard_ast = GuardAstNode("battery_soc > 20");
    ActionSignature a2;
    StoreOp op2;
    op2.target = LValueTarget("altitude");
    op2.expression = "1500";
    a2.instructions.emplace_back(op2);
    t2.transition_action = a2;
    ir.add_transition(t2);

    // Transition active -> idle on ABORT (preempts sub2)
    TransitionEdge t3;
    t3.source = "active";
    t3.target = "idle";
    t3.event = "ABORT";
    t3.source_id = "active_id";
    t3.target_id = "idle_id";
    t3.trigger = SignalTrigger("ABORT");
    t3.priority = 1;
    ir.add_transition(t3);

    // Create the verified 7-stage compilation pipeline!
    PassManager pm = PassManager::create_verified_7stage_pipeline(true);
    DiagnosticEngine diag;

    EXPECT_TRUE(pm.run(ir, diag));
    EXPECT_FALSE(diag.has_errors());

    // Verify stats collected across pipeline
    const auto& stats = pm.get_stats();
    EXPECT_GT(stats.size(), 10);

    // Verify that stage 3 (EventQueueBoundPass) annotated static capacity
    ASSERT_TRUE(ir.attributes.count("static_event_queue_capacity"));
    EXPECT_FALSE(ir.attributes["static_event_queue_capacity"].empty());
}

/**
 * @brief Verify pipeline warning diagnostics when a registered pass has unmet prerequisites.
 * @scenario Pass requiring 'NonExistentPass' is added to PassManager and executed.
 * @expected PassManager runs and emits diagnostic warning W0501 identifying unsatisfied dependency.
 */
TEST(VerifiedPipeline, MissingPrerequisitePass_EmitsDependencyWarning) {
    PassManager pm;
    pm.add_pass(std::make_unique<DummyRequiresPrereqPass>());

    FsmIr ir;
    ir.name = "TestPrereq";
    ir.initial_state_id = "s1";
    StateNode s1("s1", "s1");
    ir.states.push_back(s1);

    DiagnosticEngine diag;
    EXPECT_TRUE(pm.run(ir, diag));
    EXPECT_TRUE(diag.has_warnings());

    bool found_w0501 = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "W0501")
            found_w0501 = true;
    }
    EXPECT_TRUE(found_w0501);
}

}  // namespace

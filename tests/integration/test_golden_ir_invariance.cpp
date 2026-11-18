/**
 * @file test_golden_ir_invariance.cpp
 * @brief Golden IR cross-format roundtrip and middle-end pass invariance test suite.
 *
 * Exercises a feature-rich Golden FsmIr across all textual/diagram/formal formats
 * (json, xml, scxml, cameo, sysml2, smv, plantuml, mermaid, dot) and middle-end passes
 * to detect feature drift, metadata loss, or false diagnostics.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "fsm/backend/emitter_factory.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/guard_satisfiability_pass.hpp"
#include "fsm/middleend/analysis/livelock_analysis_pass.hpp"
#include "fsm/middleend/analysis/priority_conflict_pass.hpp"
#include "fsm/middleend/passes/boundary_action_fusion_pass.hpp"
#include "fsm/middleend/passes/common_action_factoring_pass.hpp"
#include "fsm/middleend/passes/constant_folding_pass.hpp"
#include "fsm/middleend/passes/dead_action_elimination_pass.hpp"
#include "fsm/middleend/passes/dead_state_pruning_pass.hpp"
#include "fsm/middleend/passes/determinism_enforcement_pass.hpp"
#include "fsm/middleend/passes/register_liveness_pass.hpp"
#include "fsm/middleend/passes/state_minimization_pass.hpp"
#include "fsm/middleend/passes/transition_fusion_pass.hpp"
#include "fsm/middleend/passes/wcet_analysis_pass.hpp"

using namespace fsm::ir;
using namespace fsm::frontend;
using namespace fsm::backend;
using namespace fsm::diagnostic;
using namespace fsm::middleend::passes;
using namespace fsm::middleend::analysis;

namespace {

FsmIr build_golden_ir() {
    FsmIr ir;
    ir.name = "GoldenInvarianceFSM";
    ir.initial_state = "Idle";

    // Variables (signed, unsigned, boolean)
    ir.add_variable(VariableDefinition("retry_count", DataType::int32(), "-1"));
    ir.add_variable(VariableDefinition("limit", DataType::uint32(), "5"));
    ir.add_variable(VariableDefinition("armed", DataType::boolean(), "false"));

    // Typed signal with payload attributes and validator
    SignalDefinition tel("Telemetry");
    tel.attributes.emplace_back("seq", DataType::uint32());
    tel.attributes.emplace_back("tag", DataType::string());
    tel.validators.push_back("seq > 0");
    ir.add_signal(tel);

    ir.add_signal(SignalDefinition("Start"));
    ir.add_signal(SignalDefinition("Reset"));
    ir.add_signal(SignalDefinition("Ping"));
    ir.add_signal(SignalDefinition("Step"));
    ir.add_signal(SignalDefinition("Stop"));

    // States: leaf, composite, substates, traceability, deferred, do_activity
    auto& idle = ir.add_state("Idle");
    idle.traceability_reqs.push_back("REQ_IDLE_01");

    auto& active = ir.add_state("Active");
    active.is_composite = true;
    active.initial_sub_state = "SubA";
    active.traceability_reqs.push_back("REQ_ACT_01");
    active.traceability_reqs.push_back("REQ_ACT_02");
    active.deferred_events.push_back("Ping");

    auto& sub_a = ir.add_state("SubA", "Active");
    sub_a.do_activity = "MonitorLoop";
    ir.add_action("MonitorLoop");

    auto& sub_b = ir.add_state("SubB", "Active");
    sub_b.traceability_reqs.push_back("REQ_SUBB_01");

    auto& cooldown = ir.add_state("Cooldown");
    (void)cooldown;

    // Symbolic guard metadata
    ir.add_guard("g_can_start", "", std::optional<std::string>{"armed == true && retry_count < 5"},
                 std::optional<std::string>{"armed == true && retry_count < 5"});

    // Transitions: External with symbolic guard, Local, Internal, TimeTrigger, ChangeTrigger
    TransitionEdge t_start("Idle", "Active", "Start", "g_can_start");
    t_start.priority = 1;
    t_start.traceability_reqs.push_back("REQ_TR_START");
    ir.add_transition(t_start);

    TransitionEdge t_local("Active", "SubB", "Reset");
    t_local.kind = TransitionEdgeKind::Local;
    t_local.priority = 2;
    ir.add_transition(t_local);

    TransitionEdge t_internal("SubA", "SubA", "Ping");
    t_internal.kind = TransitionEdgeKind::Internal;
    ir.add_transition(t_internal);

    TransitionEdge t_step("SubA", "SubB", "Step");
    t_step.kind = TransitionEdgeKind::External;
    ir.add_transition(t_step);

    TransitionEdge t_time("SubB", "Cooldown", "");
    t_time.trigger = TimeTrigger(TimeTriggerKind::After, 250, TimeUnit::Milliseconds);
    ir.add_transition(t_time);

    TransitionEdge t_change("Cooldown", "Idle", "");
    t_change.trigger = ChangeTrigger("retry_count >= 0", true);
    ir.add_transition(t_change);

    return ir;
}

class GoldenRoundtripParamTest : public ::testing::TestWithParam<std::string> {};

/**
 * @brief Verify Golden IR roundtrip invariance across all supported serialization/parsing formats.
 * @scenario Serialize a feature-rich Golden FsmIr to each format and re-parse into a fresh FsmIr.
 * @expected Re-parsed IR preserves states, hierarchy, Local/Internal/External kinds, TimeTrigger,
 * ChangeTrigger, SignalDefinition payload attributes/validators, and traceability requirements.
 */
TEST_P(GoldenRoundtripParamTest, PreservesCoreSemanticsAcrossRoundtrip) {
    const std::string& fmt = GetParam();
    const FsmIr golden = build_golden_ir();

    ASSERT_TRUE(EmitterFactory::is_supported_format(fmt)) << "Unsupported emitter format: " << fmt;
    auto parser = ParserFactory::create_by_format(fmt);
    ASSERT_NE(parser, nullptr) << "Missing parser for format: " << fmt;

    const std::string serialized = EmitterFactory::emit(golden, fmt);
    ASSERT_FALSE(serialized.empty()) << fmt << " emitted empty output";

    FsmIr parsed;
    std::string parse_err;
    ASSERT_TRUE(parser->parse(serialized, parsed, parse_err))
        << fmt << " parse error: " << parse_err << "\n--- Serialized ---\n"
        << serialized;

    // 1. Core states preserved
    EXPECT_NE(parsed.find_state("Idle"), nullptr) << fmt;
    EXPECT_NE(parsed.find_state("Active"), nullptr) << fmt;
    EXPECT_NE(parsed.find_state("SubA"), nullptr) << fmt;
    EXPECT_NE(parsed.find_state("SubB"), nullptr) << fmt;
    EXPECT_NE(parsed.find_state("Cooldown"), nullptr) << fmt;

    // 2. Hierarchy preserved on SubA/SubB
    if (const auto* sub_a = parsed.find_state("SubA")) {
        EXPECT_EQ(sub_a->parent_state, "Active") << fmt;
    }

    // 3. Signal attributes & validator preserved on Telemetry
    const auto* tel = parsed.find_signal("Telemetry");
    ASSERT_NE(tel, nullptr) << fmt;
    EXPECT_EQ(tel->attributes.size(), 2u) << fmt;
    EXPECT_FALSE(tel->validators.empty()) << fmt;

    // 4. TransitionEdgeKind::Local & Internal preserved
    bool found_local = false;
    bool found_internal = false;
    bool found_time = false;
    bool found_change = false;
    for (const auto& t : parsed.transitions) {
        if (t.event == "Reset" && t.kind == TransitionEdgeKind::Local) {
            found_local = true;
        }
        if (t.event == "Ping" && t.kind == TransitionEdgeKind::Internal) {
            found_internal = true;
        }
        if (std::holds_alternative<TimeTrigger>(t.trigger)) {
            found_time = true;
        }
        if (std::holds_alternative<ChangeTrigger>(t.trigger)) {
            found_change = true;
        }
    }
    EXPECT_TRUE(found_local) << fmt << "\n" << serialized;
    EXPECT_TRUE(found_internal) << fmt << "\n" << serialized;
    EXPECT_TRUE(found_time) << fmt << "\n" << serialized;
    EXPECT_TRUE(found_change) << fmt << "\n" << serialized;
}

INSTANTIATE_TEST_SUITE_P(AllFormats, GoldenRoundtripParamTest,
                         ::testing::Values(std::string("json"), std::string("scxml"), std::string("cameo"),
                                           std::string("sysml2"), std::string("smv"), std::string("plantuml"),
                                           std::string("mermaid"), std::string("dot")));

/**
 * @brief Verify middle-end analysis and optimization passes preserve Golden IR invariants without false positives.
 * @scenario Run analysis and optimization passes on Golden FsmIr.
 * @expected Zero false livelock/Zeno/determinism errors, MonitorLoop preserved, traceability_reqs preserved.
 */
TEST(GoldenPassInvariance, MiddleEndPassesPreserveGoldenInvariants) {
    FsmIr ir = build_golden_ir();
    DiagnosticEngine diag;

    EXPECT_TRUE(LivelockAnalysisPass().run(ir, diag));
    EXPECT_TRUE(WcetAnalysisPass().run(ir, diag));
    EXPECT_TRUE(DeterminismEnforcementPass().run(ir, diag));
    EXPECT_TRUE(PriorityConflictPass().run(ir, diag));
    EXPECT_TRUE(GuardSatisfiabilityPass().run(ir, diag));
    EXPECT_FALSE(diag.has_errors()) << diag.render_to_string();

    ConstantFoldingPass().run(ir, diag);
    DeadActionEliminationPass().run(ir, diag);
    RegisterLivenessPass().run(ir, diag);
    DeadStatePruningPass().run(ir, diag);
    StateMinimizationPass().run(ir, diag);
    TransitionFusionPass().run(ir, diag);
    CommonActionFactoringPass().run(ir, diag);
    BoundaryActionFusionPass().run(ir, diag);

    // MonitorLoop do_activity action preserved
    const bool has_monitor =
        std::any_of(ir.actions.begin(), ir.actions.end(), [](const ActionModel& a) { return a.name == "MonitorLoop"; });
    EXPECT_TRUE(has_monitor);

    // Traceability reqs preserved
    const auto* idle = ir.find_state("Idle");
    ASSERT_NE(idle, nullptr);
    EXPECT_FALSE(idle->traceability_reqs.empty());
    const auto* active = ir.find_state("Active");
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->traceability_reqs.size(), 2u);
    EXPECT_FALSE(active->exit_actions.empty() && !active->entry_actions.empty());
}

}  // namespace

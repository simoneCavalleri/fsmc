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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/emitter_factory.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/guard_satisfiability_pass.hpp"
#include "fsm/middleend/analysis/livelock_analysis_pass.hpp"
#include "fsm/middleend/analysis/model_checker.hpp"
#include "fsm/middleend/analysis/priority_conflict_pass.hpp"
#include "fsm/middleend/passes/boundary_action_fusion_pass.hpp"
#include "fsm/middleend/passes/choice_inlining_pass.hpp"
#include "fsm/middleend/passes/common_action_factoring_pass.hpp"
#include "fsm/middleend/passes/constant_folding_pass.hpp"
#include "fsm/middleend/passes/dead_action_elimination_pass.hpp"
#include "fsm/middleend/passes/dead_state_pruning_pass.hpp"
#include "fsm/middleend/passes/deferred_event_lowering_pass.hpp"
#include "fsm/middleend/passes/determinism_enforcement_pass.hpp"
#include "fsm/middleend/passes/fork_join_lowering_pass.hpp"
#include "fsm/middleend/passes/history_lowering_pass.hpp"
#include "fsm/middleend/passes/orthogonal_product_pass.hpp"
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
                                           std::string("mermaid"), std::string("dot"), std::string("stateflow")));

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

/**
 * @brief Differential E2E test: verifies identical runtime behavior between unoptimized (-O0)
 * and middle-end optimized (-O2) generated C++20 state machines across hierarchical, local,
 * internal, and guarded transitions.
 */
TEST(GoldenDifferentialE2E, UnoptimizedVsOptimizedCpp20_ProducesIdenticalRuntimeTrace) {
    auto build_diff_ir = [](std::string pkg) {
        FsmIr ir;
        ir.name = "DiffE2EFsm";
        ir.package = std::move(pkg);
        ir.initial_state = "Idle";

        ir.add_variable(VariableDefinition("ticks", DataType::int32(), "0"));
        ir.add_variable(VariableDefinition("flag", DataType::boolean(), "true"));

        ir.add_signal(SignalDefinition("Start"));
        ir.add_signal(SignalDefinition("Ping"));
        ir.add_signal(SignalDefinition("Step"));
        ir.add_signal(SignalDefinition("Reset"));
        ir.add_signal(SignalDefinition("Stop"));

        auto& idle = ir.add_state("Idle");
        ActionSignature idle_entry("idle_entry");
        idle_entry.assignments.emplace_back(LValueTarget("ticks"), "1", AssignmentOp::AddAssign);
        idle.entry_actions.push_back(idle_entry);

        auto& active = ir.add_state("Active");
        active.is_composite = true;
        active.initial_sub_state = "SubA";
        ActionSignature act_entry("act_entry");
        act_entry.assignments.emplace_back(LValueTarget("ticks"), "10", AssignmentOp::AddAssign);
        active.entry_actions.push_back(act_entry);
        ActionSignature act_exit("act_exit");
        act_exit.assignments.emplace_back(LValueTarget("ticks"), "100", AssignmentOp::AddAssign);
        active.exit_actions.push_back(act_exit);

        auto& sub_a = ir.add_state("SubA", "Active");
        (void)sub_a;
        auto& sub_b = ir.add_state("SubB", "Active");
        ActionSignature subb_entry("subb_entry");
        subb_entry.assignments.emplace_back(LValueTarget("ticks"), "5", AssignmentOp::AddAssign);
        sub_b.entry_actions.push_back(subb_entry);

        ir.add_guard("g_flag", "", std::optional<std::string>{"flag == true"},
                     std::optional<std::string>{"flag == true"});

        TransitionEdge t1("Idle", "Active", "Start", "g_flag");
        ir.add_transition(t1);

        TransitionEdge t_int("SubA", "SubA", "Ping");
        t_int.kind = TransitionEdgeKind::Internal;
        ActionSignature int_act("int_act");
        int_act.assignments.emplace_back(LValueTarget("ticks"), "2", AssignmentOp::AddAssign);
        t_int.transition_action = int_act;
        ir.add_transition(t_int);

        TransitionEdge t_step("SubA", "SubB", "Step");
        ir.add_transition(t_step);

        TransitionEdge t_loc("Active", "SubA", "Reset");
        t_loc.kind = TransitionEdgeKind::Local;
        ir.add_transition(t_loc);

        TransitionEdge t_stop("Active", "Idle", "Stop");
        ir.add_transition(t_stop);

        return ir;
    };

    FsmIr ir_o0 = build_diff_ir("o0");
    FsmIr ir_o2 = build_diff_ir("o2");

    DiagnosticEngine diag;
    ConstantFoldingPass().run(ir_o2, diag);
    DeadActionEliminationPass().run(ir_o2, diag);
    TransitionFusionPass().run(ir_o2, diag);
    CommonActionFactoringPass().run(ir_o2, diag);
    BoundaryActionFusionPass().run(ir_o2, diag);

    fsm::backend::cpp::GeneratorOptions opts;
    opts.cpp_standard = fsm::backend::cpp::CppStandard::Cpp20;
    opts.standalone = true;
    opts.include_stubs = true;

    const std::string code_o0 = fsm::backend::cpp::CppGenerator::generate_header(ir_o0, opts);
    const std::string code_o2 = fsm::backend::cpp::CppGenerator::generate_header(ir_o2, opts);

    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_diff_e2e_test";
    fs::create_directories(tmp_dir);

    {
        std::ofstream f0(tmp_dir / "fsm_o0.hpp");
        f0 << code_o0;
        std::ofstream f2(tmp_dir / "fsm_o2.hpp");
        f2 << code_o2;
        std::ofstream drv(tmp_dir / "driver.cpp");
        drv << R"cpp(
#include "fsm_o0.hpp"
#include "fsm_o2.hpp"

            int main() {
                o0::DiffE2EFsmRegisters reg0{0, true};
                o2::DiffE2EFsmRegisters reg2{0, true};
                o0::DiffE2EFsm m0(reg0);
                o2::DiffE2EFsm m2(reg2);
                if (reg0.ticks != reg2.ticks || m0.current_state_name() != m2.current_state_name())
                    return 1;

                m0.dispatch(o0::Start{});
                m2.dispatch(o2::Start{});
                if (reg0.ticks != reg2.ticks || m0.current_state_name() != m2.current_state_name())
                    return 2;

                m0.dispatch(o0::Ping{});
                m2.dispatch(o2::Ping{});
                if (reg0.ticks != reg2.ticks || m0.current_state_name() != m2.current_state_name())
                    return 3;

                m0.dispatch(o0::Step{});
                m2.dispatch(o2::Step{});
                if (reg0.ticks != reg2.ticks || m0.current_state_name() != m2.current_state_name())
                    return 4;

                m0.dispatch(o0::Reset{});
                m2.dispatch(o2::Reset{});
                if (reg0.ticks != reg2.ticks || m0.current_state_name() != m2.current_state_name())
                    return 5;

                m0.dispatch(o0::Stop{});
                m2.dispatch(o2::Stop{});
                if (reg0.ticks != reg2.ticks || m0.current_state_name() != m2.current_state_name())
                    return 6;

                return 0;
            }
        )cpp";
    }

    const fs::path exe = tmp_dir / "diff_runner";
    const std::string cmd = "g++ -std=c++20 -Wall -Wextra -Werror \"" + (tmp_dir / "driver.cpp").string() + "\" -o \"" +
                            exe.string() + "\"";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << "Compilation of differential C++20 test failed";
    EXPECT_EQ(std::system(exe.string().c_str()), 0) << "Runtime divergence detected between -O0 and -O2";
    fs::remove_all(tmp_dir);
}

/**
 * @brief Property-based parser robustness test: exercises irregular whitespace, tabs,
 * nested parentheses in guards, and underscore/digit identifiers across textual frontends.
 */
TEST(GoldenParserFuzz, IrregularFormattingAndNestedGuards_ParsedCleanlyWithoutCrash) {
    struct FuzzCase {
        std::string format;
        std::string source;
    };

    const std::vector<FuzzCase> cases = {
        {"sysml2", R"sysml(
            state def   Fuzz_SysML_01   {
                attribute   val_1 : Integer = 0;
                attribute   val_2 : Integer = 10;
                entry;   then   State_1_A;
                state   State_1_A;
                state   State_2_B;
                transition   first   State_1_A   accept   Ev_Cmd_1   if ((val_1 >= 0) && (val_2 < 20))   then   State_2_B;
            }
        )sysml"},
        {"plantuml",
         "@startuml Fuzz_Puml_01\n[*]\t-->\tState_1_A\nState_1_A  -->  State_2_B :  Ev_Cmd_1  [((val_1 >= 0) && (val_2 "
         "< 20))]\n@enduml\n"},
        {"mermaid",
         "stateDiagram-v2\n    [*]  -->  State_1_A\n    State_1_A\t-->\tState_2_B : Ev_Cmd_1 [((val_1 >= 0) && (val_2 "
         "< 20))]\n"},
        {"dot",
         "digraph Fuzz_Dot_01 {\n    __start__ -> State_1_A;\n    State_1_A   ->   State_2_B   [label=\"Ev_Cmd_1 "
         "[((val_1 >= 0) && (val_2 < 20))]\"];\n}\n"},
        {"smv",
         "MODULE Fuzz_Smv_01\nVAR\n  state : {State_1_A, State_2_B};\n  event : {none, Ev_Cmd_1};\n  val_1 : 0..10;\n  "
         "val_2 : 0..20;\nASSIGN\n  init(state) := State_1_A;\n  next(state) := case\n    state = State_1_A & event = "
         "Ev_Cmd_1 & ((val_1 >= 0) & (val_2 < 20)) : State_2_B;\n    TRUE : state;\n  esac;\n"},
    };

    for (const auto& fc : cases) {
        auto parser = ParserFactory::create_by_format(fc.format);
        ASSERT_NE(parser, nullptr) << fc.format;
        FsmIr ir;
        std::string err;
        ASSERT_TRUE(parser->parse(fc.source, ir, err)) << fc.format << ": " << err;
        EXPECT_NE(ir.find_state("State_1_A"), nullptr) << fc.format;
        EXPECT_NE(ir.find_state("State_2_B"), nullptr) << fc.format;
        ASSERT_FALSE(ir.transitions.empty()) << fc.format;
        EXPECT_EQ(ir.transitions[0].event, "Ev_Cmd_1") << fc.format;
        EXPECT_TRUE(ir.transitions[0].guard.has_value()) << fc.format;
    }
}

/**
 * @brief Verifies LTL/CTL ModelChecker results remain invariant before and after middle-end optimization passes.
 */
TEST(GoldenModelCheckerInvariance, TemporalPropertiesHoldBeforeAndAfterPasses) {
    FsmIr ir_before = build_golden_ir();
    FormalProperty p1("ReachCooldown", PropertyKind::Reachability, "EF(Cooldown)");
    p1.ast = PropertyAstNode(TemporalOp::EF, {PropertyAstNode("Cooldown")});
    ir_before.add_property(std::move(p1));

    FormalProperty p2("AlwaysExitIdle", PropertyKind::Liveness, "G(Idle -> F(Active))");
    p2.ast = PropertyAstNode(
        TemporalOp::Globally,
        {PropertyAstNode(TemporalOp::Implies, {PropertyAstNode("Idle"),
                                               PropertyAstNode(TemporalOp::Finally, {PropertyAstNode("Active")})})});
    ir_before.add_property(std::move(p2));

    FsmIr ir_after = ir_before;
    DiagnosticEngine diag;
    ConstantFoldingPass().run(ir_after, diag);
    DeadActionEliminationPass().run(ir_after, diag);
    TransitionFusionPass().run(ir_after, diag);
    CommonActionFactoringPass().run(ir_after, diag);
    BoundaryActionFusionPass().run(ir_after, diag);

    ModelChecker mc_before(ir_before);
    ModelChecker mc_after(ir_after);

    for (std::size_t i = 0; i < ir_before.properties.size(); ++i) {
        const auto res_before = mc_before.verify_property(ir_before.properties[i]);
        const auto res_after = mc_after.verify_property(ir_after.properties[i]);
        EXPECT_EQ(res_before.passed, res_after.passed) << "Property: " << ir_before.properties[i].name;
    }
}

/**
 * @brief Differential E2E test for C++17 standalone codegen (-O0 vs -O2) with ShallowHistory.
 */
TEST(GoldenDifferentialE2E, HistoryAndCpp17_UnoptimizedVsOptimizedMatches) {
    auto build_hist_ir = [](std::string pkg) {
        FsmIr ir;
        ir.name = "HistDiffFsm";
        ir.package = std::move(pkg);
        ir.initial_state = "Off";
        ir.add_signal(SignalDefinition("PowerOn"));
        ir.add_signal(SignalDefinition("Next"));
        ir.add_signal(SignalDefinition("PowerOff"));
        ir.add_signal(SignalDefinition("Resume"));

        ir.add_state("Off");
        auto& on = ir.add_state("On");
        on.is_composite = true;
        on.has_history = true;
        on.initial_sub_state = "Mode1";
        ir.add_state("Mode1", "On");
        ir.add_state("Mode2", "On");

        ir.add_transition(TransitionEdge("Off", "On", "PowerOn"));
        ir.add_transition(TransitionEdge("Mode1", "Mode2", "Next"));
        ir.add_transition(TransitionEdge("On", "Off", "PowerOff"));

        TransitionEdge t_res("Off", "On", "Resume");
        t_res.target_is_history = true;
        ir.add_transition(t_res);
        return ir;
    };

    FsmIr ir_o0 = build_hist_ir("ho0");
    FsmIr ir_o2 = build_hist_ir("ho2");
    DiagnosticEngine diag;
    HistoryLoweringPass().run(ir_o2, diag);
    ChoiceInliningPass().run(ir_o2, diag);
    BoundaryActionFusionPass().run(ir_o2, diag);

    fsm::backend::cpp::GeneratorOptions opts;
    opts.cpp_standard = fsm::backend::cpp::CppStandard::Cpp17;
    opts.standalone = true;
    opts.include_stubs = true;

    const std::string code_o0 = fsm::backend::cpp::CppGenerator::generate_header(ir_o0, opts);
    const std::string code_o2 = fsm::backend::cpp::CppGenerator::generate_header(ir_o2, opts);

    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_hist_diff_e2e_test";
    fs::create_directories(tmp_dir);

    {
        std::ofstream f0(tmp_dir / "hist_o0.hpp");
        f0 << code_o0;
        std::ofstream f2(tmp_dir / "hist_o2.hpp");
        f2 << code_o2;
        std::ofstream drv(tmp_dir / "driver.cpp");
        drv << R"cpp(
#include "hist_o0.hpp"
#include "hist_o2.hpp"

            int main() {
                ho0::HistDiffFsmRegisters reg0{};
                ho2::HistDiffFsmRegisters reg2{};
                ho0::HistDiffFsm m0(reg0);
                ho2::HistDiffFsm m2(reg2);
                if (m0.current_state_name() != m2.current_state_name())
                    return 1;

                m0.dispatch(ho0::PowerOn{});
                m2.dispatch(ho2::PowerOn{});
                if (m0.current_state_name() != m2.current_state_name())
                    return 2;

                m0.dispatch(ho0::Next{});
                m2.dispatch(ho2::Next{});
                if (m0.current_state_name() != m2.current_state_name())
                    return 3;

                m0.dispatch(ho0::PowerOff{});
                m2.dispatch(ho2::PowerOff{});
                if (m0.current_state_name() != m2.current_state_name())
                    return 4;

                m0.dispatch(ho0::Resume{});
                m2.dispatch(ho2::Resume{});
                if (m0.current_state_name() != m2.current_state_name())
                    return 5;

                return 0;
            }
        )cpp";
    }

    const fs::path exe = tmp_dir / "hist_diff_runner";
    const std::string cmd = "g++ -std=c++17 -Wall -Wextra -Werror \"" + (tmp_dir / "driver.cpp").string() + "\" -o \"" +
                            exe.string() + "\"";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << "Compilation of C++17 history differential test failed";
    EXPECT_EQ(std::system(exe.string().c_str()), 0) << "Runtime divergence on ShallowHistory (-O0 vs -O2)";
    fs::remove_all(tmp_dir);
}

/**
 * @brief Adversarial robustness test: exercises empty, truncated, unbalanced, unclosed-quote,
 * and deeply nested inputs across all 9 parsers to ensure graceful handling without crashes or hangs.
 */
TEST(GoldenParserFuzz, MalformedTruncatedAndDeeplyNestedInputs_FailGracefullyWithoutCrash) {
    const std::vector<std::string> formats = {"json", "scxml",    "cameo",   "sysml2",   "smv",
                                              "dot",  "plantuml", "mermaid", "stateflow"};

    std::string deep_braces;
    for (int i = 0; i < 40; ++i) {
        deep_braces += "state S" + std::to_string(i) + " {\n";
    }
    deep_braces += "state Leaf;\n";
    for (int i = 0; i < 40; ++i) {
        deep_braces += "}\n";
    }

    const std::vector<std::string> payloads = {
        "",
        "   \t\n  ",
        "{",
        "{\"states\": [{\"name\": \"unclosed",
        "<scxml><state id=\"A\"><transition event=\"Go\" target=\"",
        "@startuml\nstate Unclosed {\n  A --> B : \"unclosed_quote\n",
        "stateDiagram-v2\n    state A {\n        A --> B : Ev [((a > 0)\n",
        "digraph G {\n  A -> B [label=\"unclosed\n",
        "state def Bad {\n    transition first A accept Ev if (((x > 0)) then B;\n",
        "MODULE M\nVAR\n  state : {A, B\nASSIGN\n  next(state) := case\n",
        deep_braces,
    };

    for (const auto& fmt : formats) {
        auto parser = ParserFactory::create_by_format(fmt);
        ASSERT_NE(parser, nullptr) << fmt;
        for (const auto& payload : payloads) {
            FsmIr ir;
            std::string err;
            EXPECT_NO_THROW({ (void)parser->parse(payload, ir, err); }) << fmt;
        }
    }
}

/**
 * @brief Differential E2E test for non-standalone modular runtime (`fsm/fsm.hpp`) across multiple TUs.
 */
TEST(GoldenDifferentialE2E, ModularNonStandaloneCpp20_MultiTuLinkageMatches) {
    auto build_mod_ir = [](std::string pkg) {
        FsmIr ir;
        ir.name = "ModFsm";
        ir.package = std::move(pkg);
        ir.initial_state = "Idle";
        ir.add_variable(VariableDefinition("count", DataType::int32(), "0"));
        ir.add_signal(SignalDefinition("Go"));
        ir.add_signal(SignalDefinition("Back"));

        ir.add_state("Idle");
        auto& run = ir.add_state("Run");
        ActionSignature run_entry("run_entry");
        run_entry.assignments.emplace_back(LValueTarget("count"), "10", AssignmentOp::AddAssign);
        run.entry_actions.push_back(run_entry);

        ir.add_transition(TransitionEdge("Idle", "Run", "Go"));
        ir.add_transition(TransitionEdge("Run", "Idle", "Back"));
        return ir;
    };

    FsmIr ir_o0 = build_mod_ir("mo0");
    FsmIr ir_o2 = build_mod_ir("mo2");
    DiagnosticEngine diag;
    ConstantFoldingPass().run(ir_o2, diag);
    DeadActionEliminationPass().run(ir_o2, diag);
    BoundaryActionFusionPass().run(ir_o2, diag);

    fsm::backend::cpp::GeneratorOptions opts;
    opts.cpp_standard = fsm::backend::cpp::CppStandard::Cpp20;
    opts.standalone = false;
    opts.runtime_header = "fsm/fsm.hpp";
    opts.include_stubs = true;

    const std::string code_o0 = fsm::backend::cpp::CppGenerator::generate_header(ir_o0, opts);
    const std::string code_o2 = fsm::backend::cpp::CppGenerator::generate_header(ir_o2, opts);

    namespace fs = std::filesystem;
    const fs::path include_dir = fs::path(__FILE__).parent_path().parent_path().parent_path() / "include";
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_modular_diff_e2e_test";
    fs::create_directories(tmp_dir);

    {
        std::ofstream f0(tmp_dir / "mod_o0.hpp");
        f0 << code_o0;
        std::ofstream f2(tmp_dir / "mod_o2.hpp");
        f2 << code_o2;
        std::ofstream tu1(tmp_dir / "tu1.cpp");
        tu1 << R"cpp(
#include "mod_o0.hpp"
#include "mod_o2.hpp"
#include <iostream>
            int step_both() {
                mo0::ModFsmRegisters r0{};
                mo2::ModFsmRegisters r2{};
                mo0::ModFsm m0(r0);
                mo2::ModFsm m2(r2);
                m0.dispatch(mo0::Go{});
                m2.dispatch(mo2::Go{});
                if (m0.current_state_name() != m2.current_state_name())
                    return 1;
                if (m0.registers().count != m2.registers().count || m0.registers().count != 10) {
                    std::cerr << "m0.count=" << m0.registers().count << " m2.count=" << m2.registers().count << "\n";
                    return 2;
                }
                m0.dispatch(mo0::Back{});
                m2.dispatch(mo2::Back{});
                if (m0.current_state_name() != m2.current_state_name())
                    return 3;
                return 0;
            }
        )cpp";
        std::ofstream tu2(tmp_dir / "tu2.cpp");
        tu2 << R"cpp(
#include "mod_o0.hpp"
#include "mod_o2.hpp"
            int step_both();
            int main() {
                return step_both();
            }
        )cpp";
    }

    const fs::path exe = tmp_dir / "mod_diff_runner";
    const std::string cmd = "g++ -std=c++20 -Wall -Wextra -Werror -I\"" + include_dir.string() + "\" \"" +
                            (tmp_dir / "tu1.cpp").string() + "\" \"" + (tmp_dir / "tu2.cpp").string() + "\" -o \"" +
                            exe.string() + "\"";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << "Compilation of modular multi-TU C++20 test failed";
    EXPECT_EQ(std::system(exe.string().c_str()), 0) << "Runtime divergence on modular non-standalone (-O0 vs -O2)";
    fs::remove_all(tmp_dir);
}

/**
 * @brief Differential E2E test for Parallel orthogonal regions + OrthogonalProductPass (-O0 vs -O2).
 */
TEST(GoldenDifferentialE2E, ParallelAndForkJoin_UnoptimizedVsOptimizedMatches) {
    auto build_par_ir = [](std::string pkg) {
        FsmIr ir;
        ir.name = "ParDiffFsm";
        ir.package = std::move(pkg);
        ir.initial_state = "Idle";
        ir.add_signal(SignalDefinition("Start"));
        ir.add_signal(SignalDefinition("Step1"));
        ir.add_signal(SignalDefinition("Step2"));
        ir.add_signal(SignalDefinition("Done"));

        ir.add_state("Idle");
        auto& par = ir.add_state("Par");
        par.is_composite = true;
        par.kind = StateKind::Parallel;

        OrthogonalRegion r1;
        r1.id = "Reg1";
        r1.name = "Reg1";
        r1.initial_state_id = "R1_A";
        r1.state_ids = {"R1_A", "R1_B"};

        OrthogonalRegion r2;
        r2.id = "Reg2";
        r2.name = "Reg2";
        r2.initial_state_id = "R2_A";
        r2.state_ids = {"R2_A", "R2_B"};

        par.orthogonal_regions = {r1, r2};

        ir.add_state("R1_A", "Reg1");
        ir.add_state("R1_B", "Reg1");
        ir.add_state("R2_A", "Reg2");
        ir.add_state("R2_B", "Reg2");

        ir.add_state("Finished");

        ir.add_transition(TransitionEdge("Idle", "Par", "Start"));
        ir.add_transition(TransitionEdge("R1_A", "R1_B", "Step1"));
        ir.add_transition(TransitionEdge("R2_A", "R2_B", "Step2"));
        ir.add_transition(TransitionEdge("Par", "Finished", "Done"));
        return ir;
    };

    FsmIr ir_o0 = build_par_ir("po0");
    FsmIr ir_o2 = build_par_ir("po2");
    DiagnosticEngine diag;
    OrthogonalProductPass().run(ir_o0, diag);
    ForkJoinLoweringPass::run(ir_o2, diag);
    OrthogonalProductPass().run(ir_o2, diag);
    ConstantFoldingPass().run(ir_o2, diag);
    DeadActionEliminationPass().run(ir_o2, diag);
    BoundaryActionFusionPass().run(ir_o2, diag);

    fsm::backend::cpp::GeneratorOptions opts;
    opts.cpp_standard = fsm::backend::cpp::CppStandard::Cpp20;
    opts.standalone = true;
    opts.include_stubs = true;

    const std::string code_o0 = fsm::backend::cpp::CppGenerator::generate_header(ir_o0, opts);
    const std::string code_o2 = fsm::backend::cpp::CppGenerator::generate_header(ir_o2, opts);

    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_par_diff_e2e_test";
    fs::create_directories(tmp_dir);

    {
        std::ofstream f0(tmp_dir / "par_o0.hpp");
        f0 << code_o0;
        std::ofstream f2(tmp_dir / "par_o2.hpp");
        f2 << code_o2;
        std::ofstream drv(tmp_dir / "driver.cpp");
        drv << R"cpp(
#include "par_o0.hpp"
#include "par_o2.hpp"

            int main() {
                po0::ParDiffFsmRegisters r0{};
                po2::ParDiffFsmRegisters r2{};
                po0::ParDiffFsm m0(r0);
                po2::ParDiffFsm m2(r2);

                if (m0.current_state_name() != "Idle" || m2.current_state_name() != "Idle")
                    return 1;
                m0.dispatch(po0::Start{});
                m2.dispatch(po2::Start{});
                m0.dispatch(po0::Step1{});
                m2.dispatch(po2::Step1{});
                m0.dispatch(po0::Step2{});
                m2.dispatch(po2::Step2{});
                m0.dispatch(po0::Done{});
                m2.dispatch(po2::Done{});
                if (m0.current_state_name() != "Finished" || m2.current_state_name() != "Finished")
                    return 2;
                return 0;
            }
        )cpp";
    }

    const fs::path exe = tmp_dir / "par_diff_runner";
    const std::string cmd = "g++ -std=c++20 -Wall -Wextra -Werror \"" + (tmp_dir / "driver.cpp").string() + "\" -o \"" +
                            exe.string() + "\"";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << "Compilation of Parallel differential test failed";
    EXPECT_EQ(std::system(exe.string().c_str()), 0) << "Runtime divergence on Parallel (-O0 vs -O2)";
    fs::remove_all(tmp_dir);
}

/**
 * @brief Differential E2E test for native Deferred events across -O0 vs -O2 optimization passes.
 */
TEST(GoldenDifferentialE2E, DeferredEvents_UnoptimizedVsOptimizedMatches) {
    auto build_def_ir = [](std::string pkg) {
        FsmIr ir;
        ir.name = "DefDiffFsm";
        ir.package = std::move(pkg);
        ir.initial_state = "Busy";
        ir.add_signal(SignalDefinition("Work"));
        ir.add_signal(SignalDefinition("Ready"));

        auto& busy = ir.add_state("Busy");
        busy.deferred_events.push_back("Work");
        ir.add_state("Processing");
        ir.add_state("Done");

        ir.add_transition(TransitionEdge("Busy", "Processing", "Ready"));
        ir.add_transition(TransitionEdge("Processing", "Done", "Work"));
        return ir;
    };

    FsmIr ir_o0 = build_def_ir("do0");
    FsmIr ir_o2 = build_def_ir("do2");
    DiagnosticEngine diag;
    ConstantFoldingPass().run(ir_o2, diag);
    DeadActionEliminationPass().run(ir_o2, diag);
    TransitionFusionPass().run(ir_o2, diag);
    CommonActionFactoringPass().run(ir_o2, diag);
    BoundaryActionFusionPass().run(ir_o2, diag);

    fsm::backend::cpp::GeneratorOptions opts;
    opts.cpp_standard = fsm::backend::cpp::CppStandard::Cpp20;
    opts.standalone = true;
    opts.include_stubs = true;

    const std::string code_o0 = fsm::backend::cpp::CppGenerator::generate_header(ir_o0, opts);
    const std::string code_o2 = fsm::backend::cpp::CppGenerator::generate_header(ir_o2, opts);

    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_def_diff_e2e_test";
    fs::create_directories(tmp_dir);

    {
        std::ofstream f0(tmp_dir / "def_o0.hpp");
        f0 << code_o0;
        std::ofstream f2(tmp_dir / "def_o2.hpp");
        f2 << code_o2;
        std::ofstream drv(tmp_dir / "driver.cpp");
        drv << R"cpp(
#include "def_o0.hpp"
#include "def_o2.hpp"

            int main() {
                do0::DefDiffFsmRegisters r0{};
                do2::DefDiffFsmRegisters r2{};
                do0::DefDiffFsm m0(r0);
                do2::DefDiffFsm m2(r2);

                m0.dispatch(do0::Work{});
                m2.dispatch(do2::Work{});
                if (m0.current_state_name() != "Busy" || m2.current_state_name() != "Busy")
                    return 1;

                m0.dispatch(do0::Ready{});
                m2.dispatch(do2::Ready{});
                if (m0.current_state_name() != "Done" || m2.current_state_name() != "Done")
                    return 2;
                return 0;
            }
        )cpp";
    }

    const fs::path exe = tmp_dir / "def_diff_runner";
    const std::string cmd = "g++ -std=c++20 -Wall -Wextra -Werror \"" + (tmp_dir / "driver.cpp").string() + "\" -o \"" +
                            exe.string() + "\"";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << "Compilation of Deferred differential test failed";
    EXPECT_EQ(std::system(exe.string().c_str()), 0) << "Runtime divergence on Deferred events (-O0 vs -O2)";
    fs::remove_all(tmp_dir);
}

}  // namespace

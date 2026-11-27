/**
 * @file test_model_checker_reachability.cpp
 * @brief Unit tests for FSM formal validator, graph reachability, livelock cycles, and EFSM interval analysis.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/frontend/diagram/mermaid/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml/plantuml_parser.hpp"
#include "fsm/middleend/analysis/counterexample_exporter.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"
#include "fsm/middleend/analysis/model_checker.hpp"
#include "fsm/middleend/pass_manager.hpp"

using namespace fsm::frontend;
using namespace fsm::frontend::diagram;
using namespace fsm::middleend;
using namespace fsm::middleend::analysis;
using namespace fsm::diagnostic;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify formal validation passes for a sound state machine with zero defects.
 * @scenario Standard sequential FSM (Idle -> Running -> Paused/Stopped -> [*]).
 * @expected Validation reports model is valid with zero errors or safety warnings.
 */
TEST(ValidationPipeline, SoundStateHierarchy_PassesValidationWithoutErrors) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        Idle --> Running : StartCmd
        Running --> Paused : PauseCmd
        Paused --> Running : ResumeCmd
        Running --> Stopped : StopCmd
        Stopped --> [*]
    )";

    MermaidParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err.render_to_string();

    DiagnosticEngine diag;
    EXPECT_TRUE(PassManager::create_validation_pipeline().run(model, diag));
    EXPECT_FALSE(diag.has_errors());
}

/**
 * @brief Verify model checker detection of livelock cycles with no exit transitions.
 * @scenario Autonomous circular loop StateA -> StateB -> StateC -> StateA with no external egress.
 * @expected Validation pipeline logs livelock error diagnostic (E0401).
 */
TEST(ValidationPipeline, AutonomousClosedCycle_EmitsLivelockDiagnostic) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> StateA
        StateA --> StateB
        StateB --> StateC
        StateC --> StateA
    )";

    MermaidParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err.render_to_string();

    DiagnosticEngine diag;
    EXPECT_FALSE(PassManager::create_validation_pipeline().run(model, diag));
    bool found_livelock = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "E0401") {
            found_livelock = true;
            break;
        }
    }
    EXPECT_TRUE(found_livelock);
}

/**
 * @brief Verify model checker detects Choice nodes lacking an unconditional fallback branch.
 * @scenario Choice pseudostate branching on [IsFast] and [IsSlow] without a default fallback.
 * @expected Validation pipeline emits W0103 diagnostic.
 */
TEST(ValidationPipeline, ChoicePseudostateMissingDefaultFallback_EmitsWarningDiagnostic) {
    const std::string puml = R"(
    @startuml
    [*] --> DecisionPoint
    state DecisionPoint <<choice>>
    DecisionPoint --> ModeA : [IsFast]
    DecisionPoint --> ModeB : [IsSlow]
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err.render_to_string();

    DiagnosticEngine diag;
    PassManager::create_validation_pipeline().run(model, diag);
    bool found_choice_warning = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "W0103") {
            found_choice_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_choice_warning);
}

/**
 * @brief Verify model checker detects duplicate/conflicting guard conditions on Choice branches.
 * @scenario Choice pseudostate with two branches having identical guard '[IsFast]'.
 * @expected Validation pipeline emits W0102 warning diagnostic flagging ambiguity.
 */
TEST(ValidationPipeline, ChoiceBranchesIdenticalGuards_EmitsAmbiguityWarning) {
    const std::string puml = R"(
    @startuml
    [*] --> DecisionPoint
    state DecisionPoint <<choice>>
    DecisionPoint --> ModeA : [IsFast]
    DecisionPoint --> ModeB : [IsFast]
    DecisionPoint --> ModeC
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err.render_to_string();

    DiagnosticEngine diag;
    PassManager::create_validation_pipeline().run(model, diag);
    bool found_dup_guard = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "W0102" && d.severity == DiagnosticSeverity::Warning) {
            found_dup_guard = true;
            break;
        }
    }
    EXPECT_TRUE(found_dup_guard);
}

/**
 * @brief Verify model checker detects deadlock/trap states having no outgoing transitions.
 * @scenario Active state transitions to 'TrapState', which has 0 outgoing transitions and is not marked Final.
 * @expected Validation pipeline emits W0202 warning diagnostic.
 */
TEST(ValidationPipeline, UnhandledTerminalState_EmitsDeadlockTrapWarning) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Active
        Active --> TrapState : ErrorEvent
    )";

    MermaidParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err.render_to_string();

    DiagnosticEngine diag;
    PassManager::create_validation_pipeline().run(model, diag);
    bool found_deadlock_warning = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "W0202" && d.severity == DiagnosticSeverity::Warning) {
            found_deadlock_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_deadlock_warning);
}

/**
 * @brief Verify model checker detects non-deterministic transition conflicts for identical events.
 * @scenario State 'Idle' has two unconditional transitions triggered by identical event 'StartCmd'.
 * @expected Validation pipeline emits SafetyCritical diagnostic (W_NONDETERMINISTIC_CONFLICT).
 */
TEST(ValidationPipeline, ConflictingTransitionsSameEventAndPriority_EmitsDeterminismError) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        Idle --> StateA : StartCmd
        Idle --> StateB : StartCmd
    )";

    MermaidParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err.render_to_string();

    DiagnosticEngine diag;
    PassManager::create_validation_pipeline().run(model, diag);
    bool found_conflict = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "W_NONDETERMINISTIC_CONFLICT" && d.severity == DiagnosticSeverity::SafetyCritical) {
            found_conflict = true;
            break;
        }
    }
    EXPECT_TRUE(found_conflict);
}

/**
 * @brief Verify model checker detects duplicate timer transitions originating from the same state.
 * @scenario State 'Active' has two timer transitions with identical duration 'after_500ms'.
 * @expected Validation pipeline emits W0402 warning diagnostic.
 */
TEST(ValidationPipeline, DuplicateTimerTransitionsSameState_EmitsWarning) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Active
        Active --> StateA : after_500ms
        Active --> StateB : after_500ms
    )";

    MermaidParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err.render_to_string();

    DiagnosticEngine diag;
    PassManager::create_validation_pipeline().run(model, diag);
    bool found_timer_warning = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "W0402" && d.severity == DiagnosticSeverity::Warning) {
            found_timer_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_timer_warning);
}

/**
 * @brief Verify EFSM Interval Analysis detects unsatisfiable guard conditions across data paths.
 * @scenario EFSM with batteryLevel = 20, transition adds 10 (range [30, 30]), followed by guard 'batteryLevel > 100'.
 * @expected Analyzer flags the dead branch with warning diagnostic identifying unreachable transition.
 */
TEST(EfsmIntervalAnalysis, OutOfRangeVariableGuardCondition_FlagsUnsatisfiableBranch) {
    FsmIr model;
    model.name = "PowerFSM";
    model.initial_state = "Idle";

    model.add_state("Idle");
    model.add_state("Active");
    model.add_state("Turbo");

    VariableDefinition var;
    var.name = "batteryLevel";
    var.type = "float";
    var.initial_value = "20.0";
    model.add_variable(var);

    TransitionEdge t1("t1", "Idle", "Active", SignalTrigger("Start"));
    ActionSignature act_sig("Charge");
    act_sig.assignments.push_back(ActionAssignment("batteryLevel", "batteryLevel + 10"));
    t1.transition_action = act_sig;
    model.add_transition(t1);

    TransitionEdge t2("t2", "Active", "Turbo", SignalTrigger("Boost"));
    t2.guard = "batteryLevel > 100.0";
    model.add_transition(t2);

    DiagnosticEngine diag;
    EFSMIntervalAnalyzer analyzer(model);
    auto findings = analyzer.analyze(diag);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].variable_name, "batteryLevel");
    EXPECT_EQ(findings[0].source_state, "Active");
    EXPECT_EQ(findings[0].target_state, "Turbo");
    EXPECT_TRUE(diag.has_warnings());
}

/**
 * @brief Regression: `FsmValidator::validate_reachability` traverses `target_ids` / `multi_target_ids`
 * on lowered fork transitions so secondary fork branch states are not falsely reported unreachable.
 * @scenario Validate an FSM with `Init -> {RegA, RegB}` via `target_ids`.
 * @expected Zero `Reachability` warnings are emitted.
 */
TEST(ValidationPipeline, MultiTargetForkTransition_NoFalseUnreachableWarning) {
    FsmIr model;
    model.name = "MultiTargetVal";
    model.initial_state = "Init";
    model.add_state("Init");
    model.add_state("RegA");
    model.add_state("RegB");

    TransitionEdge fork_edge("Init", "RegA", "EvFork");
    fork_edge.target_ids = {"RegA", "RegB"};
    model.add_transition(std::move(fork_edge));

    DiagnosticEngine diag;
    PassManager::create_validation_pipeline().run(model, diag);
    for (const auto& d : diag.get_diagnostics()) {
        EXPECT_NE(d.code, "W0201") << d.message;
    }
}

/**
 * @brief Regression (BUG-53): ModelChecker enters only `initial_sub_state` on composite states and skips
 * transitions with static `false` guards, avoiding false-positive violations on unreachable substates.
 */
TEST(ModelCheckerReachability, CompositeInitialSubstateAndStaticFalseGuard_NoFalseViolation) {
    FsmIr model;
    model.name = "CompReach";
    model.initial_state = "Active";
    auto& active = model.add_state("Active");
    active.is_composite = true;
    active.initial_sub_state = "SafeSub";
    model.add_state("SafeSub", "Active");
    model.add_state("UnreachableHazard", "Active");

    TransitionEdge dead_tr("SafeSub", "UnreachableHazard", "EvGo");
    dead_tr.set_guard("false");
    model.add_transition(std::move(dead_tr));

    FormalProperty prop("NoHazard", PropertyKind::Safety, "G (state != UnreachableHazard)");
    prop.ast = PropertyAstNode::parse(prop.raw_formula);

    ModelChecker checker(model);
    const auto res = checker.verify_property(prop);
    EXPECT_TRUE(res.passed);
}

/**
 * @brief Regression (BUG-58): CounterexampleGTestExporter emits `std::chrono::milliseconds(...)` steps
 * for TimeTrigger transitions and populates witness assignments for both in-ports and event payloads in
 * compound symbolic guards.
 */
TEST(ModelCheckerReachability, CounterexampleGTestExporter_EmitsTimerStepAndPayloadWitnesses) {
    FsmIr model;
    model.name = "ReactorCtrl";
    model.initial_state = "Nominal";
    model.add_state("Nominal");
    model.add_state("Pressurized");
    model.add_state("Meltdown");
    model.add_port(PortDefinition("ambient", DataType::int32(), PortDirection::In));
    SignalDefinition sig("EvBoost");
    sig.attributes.emplace_back("delta", DataType::int32());
    model.add_signal(sig);
    model.add_guard("ambient_gt_20", "", std::string("ambient > 20"), std::string("ambient > 20"));
    model.add_guard("delta_gt_5", "", std::string("delta > 5"), std::string("delta > 5"));

    TransitionEdge t1("Nominal", "Pressurized", "EvBoost");
    t1.set_guard("ambient_gt_20 && delta_gt_5");
    model.add_transition(std::move(t1));

    TransitionEdge t2("Pressurized", "Meltdown", "after_100ms");
    t2.trigger = TimeTrigger{TimeTriggerKind::After, 100};
    model.add_transition(std::move(t2));

    FormalProperty prop("NoMeltdown", PropertyKind::Safety, "G (state != Meltdown)");
    prop.ast = PropertyAstNode::parse(prop.raw_formula);

    ModelChecker checker(model);
    const auto res = checker.verify_property(prop);
    ASSERT_FALSE(res.passed);

    const std::string gtest = CounterexampleGTestExporter::export_gtest(model, res, "reactor.hpp", "fsm_generated");
    EXPECT_NE(gtest.find("in.ambient = 20 + 1;"), std::string::npos) << gtest;
    EXPECT_NE(gtest.find("ev.delta = 5 + 1;"), std::string::npos) << gtest;
    EXPECT_NE(gtest.find("sm.step(std::chrono::milliseconds(100), in, out);"), std::string::npos) << gtest;
    EXPECT_EQ(gtest.find("sm.dispatch(after_100ms"), std::string::npos) << gtest;
}

}  // namespace

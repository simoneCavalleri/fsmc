#include <gtest/gtest.h>

#include <string>

#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"

using namespace fsm::frontend;
using namespace fsm::frontend::diagram;
using namespace fsm::middleend;
using namespace fsm::middleend::analysis;
using namespace fsm::diagnostic;
using namespace fsm::ir;

namespace {

/**
 * @brief Test Intent: Verify formal validation passes for a sound state machine with zero defects.
 *
 * Scenario:
 * - Validate standard FSM (Idle -> Running -> Paused/Stopped -> [*]).
 * - Verify validation result has is_valid == true and 0 errors.
 */
TEST(ModelCheckerTest, SoundModelVerification) {
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
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    const auto validation = FsmValidator::validate(model);
    EXPECT_TRUE(validation.is_valid);
    EXPECT_TRUE(validation.errors.empty());
}

/**
 * @brief Test Intent: Verify model checker detection of livelock cycles with no exit transitions.
 *
 * Scenario:
 * - Parse circular loop: StateA -> StateB -> StateC -> StateA.
 * - Verify diagnostic engine emits SafetyCritical diagnostic for Livelock.
 */
TEST(ModelCheckerTest, LivelockCycleDetection) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> StateA
        StateA --> StateB
        StateB --> StateC
        StateC --> StateA
    )";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    const auto validation = FsmValidator::validate(model);
    bool found_livelock = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "Livelock" && diag.severity == DiagnosticSeverity::SafetyCritical) {
            found_livelock = true;
            break;
        }
    }
    EXPECT_TRUE(found_livelock);
}

/**
 * @brief Test Intent: Verify model checker detects Choice nodes lacking an unconditional fallback branch.
 *
 * Scenario:
 * - Choice node branches on [IsFast] and [IsSlow] without a default else branch.
 * - Verify SafetyCritical Choice diagnostic is emitted.
 */
TEST(ModelCheckerTest, ChoiceMissingFallback) {
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
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    const auto validation = FsmValidator::validate(model);
    bool found_choice_warning = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "Choice" && diag.severity == DiagnosticSeverity::SafetyCritical) {
            found_choice_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_choice_warning);
}

/**
 * @brief Test Intent: Verify model checker detects duplicate/conflicting guard conditions on Choice branches.
 *
 * Scenario:
 * - Choice node has two outgoing branches with identical guard `[IsFast]`.
 * - Verify warning diagnostic is emitted for non-deterministic choice guards.
 */
TEST(ModelCheckerTest, ChoiceDuplicateGuards) {
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
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    const auto validation = FsmValidator::validate(model);
    bool found_dup_guard = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "Choice" && diag.severity == DiagnosticSeverity::Warning) {
            found_dup_guard = true;
            break;
        }
    }
    EXPECT_TRUE(found_dup_guard);
}

/**
 * @brief Test Intent: Verify model checker detects deadlock/trap states (states with no exit transitions).
 *
 * Scenario:
 * - Active transitions to TrapState on ErrorEvent, and TrapState has 0 outgoing transitions.
 * - Verify Deadlock diagnostic warning is reported.
 */
TEST(ModelCheckerTest, DeadlockTrapState) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Active
        Active --> TrapState : ErrorEvent
    )";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    const auto validation = FsmValidator::validate(model);
    bool found_deadlock_warning = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "Deadlock" && diag.severity == DiagnosticSeverity::Warning) {
            found_deadlock_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_deadlock_warning);
}

/**
 * @brief Test Intent: Verify model checker detects non-deterministic transition conflicts for identical events.
 *
 * Scenario:
 * - State Idle has two unconditional transitions for the same event `StartCmd` (one to StateA, one to StateB).
 * - Verify SafetyCritical Determinism conflict diagnostic is emitted.
 */
TEST(ModelCheckerTest, NondeterministicTransitionConflict) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        Idle --> StateA : StartCmd
        Idle --> StateB : StartCmd
    )";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    const auto validation = FsmValidator::validate(model);
    bool found_conflict = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "Determinism" && diag.severity == DiagnosticSeverity::SafetyCritical) {
            found_conflict = true;
            break;
        }
    }
    EXPECT_TRUE(found_conflict);
}

/**
 * @brief Test Intent: Verify model checker detects duplicate timer transitions from the same state.
 *
 * Scenario:
 * - State Active has two transitions with identical timer duration `after_500ms`.
 * - Verify TimedTransition diagnostic warning is emitted.
 */
TEST(ModelCheckerTest, DuplicateTimerTransitions) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Active
        Active --> StateA : after_500ms
        Active --> StateB : after_500ms
    )";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    const auto validation = FsmValidator::validate(model);
    bool found_timer_warning = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "TimedTransition" && diag.severity == DiagnosticSeverity::Warning) {
            found_timer_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_timer_warning);
}

/**
 * @brief Test Intent: Verify EFSM Interval Analysis detects unsatisfiable guard conditions across data paths.
 *
 * Scenario:
 * - Define EFSM with batteryLevel initialized to 20.
 * - Transition Idle -> Active with assignment batteryLevel = batteryLevel + 10 (range [30, 30]).
 * - Transition Active -> Turbo with unsatisfiable guard 'batteryLevel > 100'.
 * - Verify EFSMIntervalAnalyzer flags the dead branch with W_EFSM_UNSATISFIABLE_GUARD warning.
 */
TEST(ModelCheckerTest, EFSMDataPathIntervalAnalysis) {
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
    t1.action_sig = act_sig;
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

}  // namespace

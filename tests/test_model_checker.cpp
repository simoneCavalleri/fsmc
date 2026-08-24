#include <gtest/gtest.h>

#include <string>

#include "fsm/frontend/mermaid_parser.hpp"
#include "fsm/frontend/plantuml_parser.hpp"
#include "fsm/middleend/fsm_validator.hpp"

using namespace fsm::codegen;

namespace {

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

}  // namespace

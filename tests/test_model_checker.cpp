#include <cassert>
#include <iostream>
#include <string>

#include "codegen/fsm_validator.hpp"
#include "codegen/mermaid_parser.hpp"
#include "codegen/plantuml_parser.hpp"

using namespace fsm::codegen;

namespace {

void test_sound_model_verification() {
    std::cout << "[TEST] Running test_sound_model_verification...\n";

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
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(mmd, model, err);
    assert(is_parsed);

    const auto validation = FsmValidator::validate(model);
    assert(validation.is_valid);
    assert(validation.errors.empty());

    std::cout << "[PASS] test_sound_model_verification passed.\n";
}

void test_livelock_cycle_detection() {
    std::cout << "[TEST] Running test_livelock_cycle_detection...\n";

    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> StateA
        StateA --> StateB
        StateB --> StateC
        StateC --> StateA
    )";

    MermaidParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(mmd, model, err);
    assert(is_parsed);

    const auto validation = FsmValidator::validate(model);
    bool found_livelock = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "Livelock" && diag.severity == DiagnosticSeverity::SafetyCritical) {
            found_livelock = true;
            break;
        }
    }
    assert(found_livelock);

    std::cout << "[PASS] test_livelock_cycle_detection passed.\n";
}

void test_choice_missing_fallback() {
    std::cout << "[TEST] Running test_choice_missing_fallback...\n";

    const std::string puml = R"(
    @startuml
    [*] --> DecisionPoint
    state DecisionPoint <<choice>>
    DecisionPoint --> ModeA : [IsFast]
    DecisionPoint --> ModeB : [IsSlow]
    @enduml
    )";

    PlantUmlParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(puml, model, err);
    assert(is_parsed);

    const auto validation = FsmValidator::validate(model);
    bool found_choice_warning = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "Choice" && diag.severity == DiagnosticSeverity::SafetyCritical) {
            found_choice_warning = true;
            break;
        }
    }
    assert(found_choice_warning);

    std::cout << "[PASS] test_choice_missing_fallback passed.\n";
}

void test_choice_duplicate_guards() {
    std::cout << "[TEST] Running test_choice_duplicate_guards...\n";

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
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(puml, model, err);
    assert(is_parsed);

    const auto validation = FsmValidator::validate(model);
    bool found_dup_guard = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "Choice" && diag.severity == DiagnosticSeverity::Warning) {
            found_dup_guard = true;
            break;
        }
    }
    assert(found_dup_guard);

    std::cout << "[PASS] test_choice_duplicate_guards passed.\n";
}

void test_deadlock_trap_state() {
    std::cout << "[TEST] Running test_deadlock_trap_state...\n";

    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Active
        Active --> TrapState : ErrorEvent
    )";

    MermaidParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(mmd, model, err);
    assert(is_parsed);

    const auto validation = FsmValidator::validate(model);
    bool found_deadlock_warning = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "Deadlock" && diag.severity == DiagnosticSeverity::Warning) {
            found_deadlock_warning = true;
            break;
        }
    }
    assert(found_deadlock_warning);

    std::cout << "[PASS] test_deadlock_trap_state passed.\n";
}

void test_nondeterministic_transition_conflict() {
    std::cout << "[TEST] Running test_nondeterministic_transition_conflict...\n";

    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        Idle --> StateA : StartCmd
        Idle --> StateB : StartCmd
    )";

    MermaidParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(mmd, model, err);
    assert(is_parsed);

    const auto validation = FsmValidator::validate(model);
    bool found_conflict = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "Determinism" && diag.severity == DiagnosticSeverity::SafetyCritical) {
            found_conflict = true;
            break;
        }
    }
    assert(found_conflict);

    std::cout << "[PASS] test_nondeterministic_transition_conflict passed.\n";
}

void test_duplicate_timer_transitions() {
    std::cout << "[TEST] Running test_duplicate_timer_transitions...\n";

    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Active
        Active --> StateA : after_500ms
        Active --> StateB : after_500ms
    )";

    MermaidParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(mmd, model, err);
    assert(is_parsed);

    const auto validation = FsmValidator::validate(model);
    bool found_timer_warning = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "TimedTransition" && diag.severity == DiagnosticSeverity::Warning) {
            found_timer_warning = true;
            break;
        }
    }
    assert(found_timer_warning);

    std::cout << "[PASS] test_duplicate_timer_transitions passed.\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "     RUNNING FORMAL MODEL CHECKER TESTS \n"
              << "========================================\n";

    test_sound_model_verification();
    test_livelock_cycle_detection();
    test_choice_missing_fallback();
    test_choice_duplicate_guards();
    test_deadlock_trap_state();
    test_nondeterministic_transition_conflict();
    test_duplicate_timer_transitions();

    std::cout << "========================================\n"
              << "  ALL MODEL CHECKER TESTS PASSED (7/7)! \n"
              << "========================================\n";
    return 0;
}

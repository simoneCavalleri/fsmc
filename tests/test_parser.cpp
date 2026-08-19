#include <cassert>
#include <iostream>
#include <optional>
#include <string>

#include "codegen/fsm_validator.hpp"
#include "codegen/mermaid_parser.hpp"
#include "codegen/plantuml_parser.hpp"

using namespace fsm::codegen;

namespace {

void test_mermaid_parsing() {
    std::cout << "[TEST] Running test_mermaid_parsing...\n";

    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        state "Waiting for CAN" as WaitingForCan
        Idle --> WaitingForCan : CmdStart [CanStartGuard] / OnStartAction
        Idle --> Idle : CmdStop / OnStopAction
        WaitingForCan --> Running : CanOk [IsReady]
        Running --> Idle : CmdStop
    )";

    MermaidParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(mmd, model, err);

    assert(is_parsed);
    assert(model.initial_state == "Idle");
    assert(model.states.size() == 3);   // Idle, WaitingForCan, Running
    assert(model.events.size() == 3);   // CmdStart, CmdStop, CanOk
    assert(model.guards.size() == 2);   // CanStartGuard, IsReady
    assert(model.actions.size() == 2);  // OnStartAction, OnStopAction
    assert(model.transitions.size() == 4);

    const auto validation = FsmValidator::validate(model);
    assert(validation.is_valid);
    assert(validation.errors.empty());

    std::cout << "[PASS] test_mermaid_parsing passed.\n";
}

void test_plantuml_parsing() {
    std::cout << "[TEST] Running test_plantuml_parsing...\n";

    const std::string puml = R"(
    @startuml
    [*] --> Standby
    Standby -> Processing : StartTask [HasWork] / InitTask
    Processing -> Standby : StopTask / Cleanup
    @enduml
    )";

    PlantUmlParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(puml, model, err);

    assert(is_parsed);
    assert(model.initial_state == "Standby");
    assert(model.states.size() == 2);
    assert(model.events.size() == 2);
    assert(model.guards.size() == 1);
    assert(model.actions.size() == 2);
    assert(model.transitions.size() == 2);

    const auto validation = FsmValidator::validate(model);
    assert(validation.is_valid);

    std::cout << "[PASS] test_plantuml_parsing passed.\n";
}

void test_validator_detects_errors() {
    std::cout << "[TEST] Running test_validator_detects_errors...\n";

    FsmModel model;
    model.initial_state = "Idle";
    model.add_state("Idle");
    // Add transition to unknown target
    model.transitions.emplace_back("Idle", "UnknownTarget", "MyEvent", std::nullopt, std::nullopt, "");

    const auto validation = FsmValidator::validate(model);
    assert(!validation.is_valid);
    assert(!validation.errors.empty());

    std::cout << "[PASS] test_validator_detects_errors passed.\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "     RUNNING FSM-GEN PARSER TESTS       \n"
              << "========================================\n";

    test_mermaid_parsing();
    test_plantuml_parsing();
    test_validator_detects_errors();

    std::cout << "========================================\n"
              << "     ALL PARSER TESTS PASSED (3/3)!     \n"
              << "========================================\n";
    return 0;
}

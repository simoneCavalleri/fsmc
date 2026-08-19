#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cpp_generator.hpp"
#include "codegen/fsm_validator.hpp"
#include "codegen/mermaid_parser.hpp"
#include "codegen/plantuml_parser.hpp"

using namespace fsm::codegen;

namespace {

void test_plantuml_composite_state_parsing() {
    std::cout << "[TEST] Running test_plantuml_composite_state_parsing...\n";

    const std::string puml = R"(
    @startuml
    [*] --> Active

    state Active {
        [*] --> Idle
        Idle --> Processing : StartTask
        Processing --> Idle : TaskDone
    }

    Active --> Terminated : Shutdown
    @enduml
    )";

    PlantUmlParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(puml, model, err);

    assert(is_parsed);
    assert(model.initial_state == "Active");
    assert(model.states.size() == 4); // Active, Idle, Processing, Terminated

    const auto* active_state = model.find_state("Active");
    assert(active_state != nullptr);
    assert(active_state->is_composite);
    assert(active_state->initial_sub_state == "Idle");

    const auto* idle_state = model.find_state("Idle");
    assert(idle_state != nullptr);
    assert(idle_state->parent_state == "Active");

    const auto* processing_state = model.find_state("Processing");
    assert(processing_state != nullptr);
    assert(processing_state->parent_state == "Active");

    const auto validation = FsmValidator::validate(model);
    assert(validation.is_valid);

    std::cout << "[PASS] test_plantuml_composite_state_parsing passed.\n";
}

void test_mermaid_composite_state_parsing() {
    std::cout << "[TEST] Running test_mermaid_composite_state_parsing...\n";

    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Session
        state Session {
            [*] --> Connected
            Connected --> Disconnected : Logout
        }
        Session --> Off : PowerOff
    )";

    MermaidParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(mmd, model, err);

    assert(is_parsed);
    assert(model.initial_state == "Session");

    const auto* session_state = model.find_state("Session");
    assert(session_state != nullptr);
    assert(session_state->is_composite);
    assert(session_state->initial_sub_state == "Connected");

    const auto* conn_state = model.find_state("Connected");
    assert(conn_state != nullptr);
    assert(conn_state->parent_state == "Session");

    const auto validation = FsmValidator::validate(model);
    assert(validation.is_valid);

    std::cout << "[PASS] test_mermaid_composite_state_parsing passed.\n";
}

} // namespace

int main() {
    std::cout << "========================================\n"
              << "     RUNNING UML 2.5 HFSM TESTS         \n"
              << "========================================\n";

    test_plantuml_composite_state_parsing();
    test_mermaid_composite_state_parsing();

    std::cout << "========================================\n"
              << "     ALL HFSM TESTS PASSED (2/2)!       \n"
              << "========================================\n";
    return 0;
}

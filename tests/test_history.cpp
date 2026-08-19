#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cpp_generator.hpp"
#include "codegen/fsm_validator.hpp"
#include "codegen/mermaid_parser.hpp"
#include "codegen/plantuml_parser.hpp"

using namespace fsm::codegen;

namespace {

void test_history_target_parsing_plantuml() {
    std::cout << "[TEST] Running test_history_target_parsing_plantuml...\n";

    const std::string puml = R"(
    @startuml
    [*] --> Standby

    state Operating {
        [*] --> Step1
        Step1 --> Step2 : NextStep
    }

    Operating --> Paused : Pause
    Paused --> Operating[H] : Resume
    @enduml
    )";

    PlantUmlParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(puml, model, err);

    assert(is_parsed);
    const auto* op_state = model.find_state("Operating");
    assert(op_state != nullptr);
    assert(op_state->has_history);

    bool found_history_transition = false;
    for (const auto& t : model.transitions) {
        if (t.source == "Paused" && t.target == "Operating" && t.target_is_history) {
            found_history_transition = true;
        }
    }
    assert(found_history_transition);

    std::cout << "[PASS] test_history_target_parsing_plantuml passed.\n";
}

void test_deep_history_target_parsing_mermaid() {
    std::cout << "[TEST] Running test_deep_history_target_parsing_mermaid...\n";

    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Standby
        state Operating {
            [*] --> StageA
        }
        Operating --> Suspended : Interrupt
        Suspended --> Operating[H*] : Recover
    )";

    MermaidParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(mmd, model, err);

    assert(is_parsed);
    const auto* op_state = model.find_state("Operating");
    assert(op_state != nullptr);
    assert(op_state->has_deep_history);

    std::cout << "[PASS] test_deep_history_target_parsing_mermaid passed.\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "     RUNNING UML 2.5 HISTORY TESTS      \n"
              << "========================================\n";

    test_history_target_parsing_plantuml();
    test_deep_history_target_parsing_mermaid();

    std::cout << "========================================\n"
              << "     ALL HISTORY TESTS PASSED (2/2)!    \n"
              << "========================================\n";
    return 0;
}

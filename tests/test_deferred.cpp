#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cpp_generator.hpp"
#include "codegen/fsm_validator.hpp"
#include "codegen/mermaid_parser.hpp"
#include "codegen/plantuml_parser.hpp"

using namespace fsm::codegen;

namespace {

void test_deferred_events_plantuml() {
    std::cout << "[TEST] Running test_deferred_events_plantuml...\n";

    const std::string puml = R"(
    @startuml
    [*] --> Initializing

    Initializing : defer RequestCmd
    Initializing : defer DataPacket

    Initializing --> Ready : InitDone
    Ready --> Processing : RequestCmd
    @enduml
    )";

    PlantUmlParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(puml, model, err);

    assert(is_parsed);
    const auto* init_state = model.find_state("Initializing");
    assert(init_state != nullptr);
    assert(init_state->deferred_events.size() == 2);
    assert(init_state->deferred_events[0] == "RequestCmd");
    assert(init_state->deferred_events[1] == "DataPacket");

    std::cout << "[PASS] test_deferred_events_plantuml passed.\n";
}

void test_deferred_events_mermaid() {
    std::cout << "[TEST] Running test_deferred_events_mermaid...\n";

    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Booting
        Booting : defer UserInput
        Booting --> Running : BootComplete
    )";

    MermaidParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(mmd, model, err);

    assert(is_parsed);
    const auto* boot_state = model.find_state("Booting");
    assert(boot_state != nullptr);
    assert(boot_state->deferred_events.size() == 1);
    assert(boot_state->deferred_events[0] == "UserInput");

    std::cout << "[PASS] test_deferred_events_mermaid passed.\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "     RUNNING UML 2.5 DEFERRED TESTS     \n"
              << "========================================\n";

    test_deferred_events_plantuml();
    test_deferred_events_mermaid();

    std::cout << "========================================\n"
              << "     ALL DEFERRED TESTS PASSED (2/2)!   \n"
              << "========================================\n";
    return 0;
}

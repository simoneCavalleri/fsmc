#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cpp_generator.hpp"
#include "codegen/fsm_validator.hpp"
#include "codegen/plantuml_parser.hpp"

using namespace fsm::codegen;

namespace {

void test_choice_pseudostate_parsing_and_codegen() {
    std::cout << "[TEST] Running test_choice_pseudostate_parsing_and_codegen...\n";

    const std::string puml = R"(
    @startuml
    [*] --> Idle

    state AuthChoice <<choice>>

    Idle --> AuthChoice : LoginCmd
    AuthChoice --> AdminView : [IsAdminGuard] / GrantAdminAction
    AuthChoice --> UserView : [IsUserGuard] / GrantUserAction
    @enduml
    )";

    PlantUmlParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(puml, model, err);

    assert(is_parsed);
    assert(model.choice_nodes.size() == 1);
    assert(model.choice_nodes[0].name == "AuthChoice");
    assert(model.states.size() == 3); // Idle, AdminView, UserView
    assert(model.guards.size() == 2);
    assert(model.actions.size() == 2);

    const auto validation = FsmValidator::validate(model);
    assert(validation.is_valid);

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    opts.standalone = false;
    const std::string code = CppGenerator::generate_header(model, opts);

    // Verify expanded transition table rows
    assert(code.find("fsm::row<Idle, LoginCmd, AdminView>::when<IsAdminGuard>::then<GrantAdminAction>") != std::string::npos);
    assert(code.find("fsm::row<Idle, LoginCmd, UserView>::when<IsUserGuard>::then<GrantUserAction>") != std::string::npos);

    std::cout << "[PASS] test_choice_pseudostate_parsing_and_codegen passed.\n";
}

} // namespace

int main() {
    std::cout << "========================================\n"
              << "     RUNNING UML 2.5 CHOICE TESTS       \n"
              << "========================================\n";

    test_choice_pseudostate_parsing_and_codegen();

    std::cout << "========================================\n"
              << "     ALL CHOICE TESTS PASSED (1/1)!     \n"
              << "========================================\n";
    return 0;
}

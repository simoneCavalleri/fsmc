#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/middleend/fsm_validator.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify Choice pseudostate expansion and code generation.
 *
 * Scenario:
 * - Parse PlantUML containing `state AuthChoice <<choice>>` and conditional outgoing branches.
 * - Verify Choice node is captured as a choice_node in the Formal IR.
 * - Verify C++ code generator expands the choice into direct guarded rows in the transition table
 *   (e.g., row<Idle, LoginCmd, AdminView>::when<IsAdminGuard> and row<Idle, LoginCmd, UserView>::when<IsUserGuard>).
 */
TEST(ChoiceTest, ChoicePseudostateParsingAndCodegen) {
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
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    ASSERT_EQ(model.choice_nodes.size(), 1u);
    EXPECT_EQ(model.choice_nodes[0].name, "AuthChoice");
    EXPECT_EQ(model.states.size(), 3u);  // Idle, AdminView, UserView
    EXPECT_EQ(model.guards.size(), 2u);
    EXPECT_EQ(model.actions.size(), 2u);

    const auto validation = FsmValidator::validate(model);
    EXPECT_TRUE(validation.is_valid);

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    opts.standalone = false;
    const std::string code = CppGenerator::generate_header(model, opts);

    // Verify expanded transition table rows
    EXPECT_NE(code.find("fsm::row<Idle, LoginCmd, AdminView>::when<IsAdminGuard>::then<GrantAdminAction>"),
              std::string::npos);
    EXPECT_NE(code.find("fsm::row<Idle, LoginCmd, UserView>::when<IsUserGuard>::then<GrantUserAction>"),
              std::string::npos);
}

}  // namespace

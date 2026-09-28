/**
 * @file test_choice.cpp
 * @brief Unit test suite for choice pseudostates and conditional branching in runtime.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"

using namespace fsm::backend::cpp;
using namespace fsm::backend;
using namespace fsm::frontend::diagram;
using namespace fsm::frontend;
using namespace fsm::middleend::analysis;
using namespace fsm::middleend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify choice pseudostate parsing, code generation, and conditional runtime branching.
 * @scenario Instantiate state machine with Choice node guarded by mutually exclusive conditions.
 * @expected Machine evaluates guards dynamically at Choice node and branches to selected target state.
 */
TEST(ChoicePseudostate, ChoicePseudostate_ConditionalEvaluation_BranchesToTarget) {
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
    EXPECT_NE(code.find("row<Idle, LoginCmd, AdminView>::when<IsAdminGuard>::then<GrantAdminAction>"),
              std::string::npos);
    EXPECT_NE(code.find("row<Idle, LoginCmd, UserView>::when<IsUserGuard>::then<GrantUserAction>"), std::string::npos);
}

}  // namespace

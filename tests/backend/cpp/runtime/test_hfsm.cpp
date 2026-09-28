/**
 * @file test_hfsm.cpp
 * @brief Unit test suite for hierarchical composite states and HFSM event dispatching.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
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
 * @brief Verify PlantUML composite state parsing and runtime initialization.
 * @scenario Parse PlantUML diagram declaring nested sub-states and initial sub-state.
 * @expected Composite hierarchy formed and machine initializes in initial sub-state.
 */
TEST(HfsmHierarchy, PlantUmlCompositeState_NestedHierarchy_ParsedCorrectly) {
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
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    EXPECT_EQ(model.initial_state, "Active");
    EXPECT_EQ(model.states.size(), 4u);  // Active, Idle, Processing, Terminated

    const auto* active_state = model.find_state("Active");
    ASSERT_NE(active_state, nullptr);
    EXPECT_TRUE(active_state->is_composite);
    EXPECT_EQ(active_state->initial_sub_state, "Idle");

    const auto* idle_state = model.find_state("Idle");
    ASSERT_NE(idle_state, nullptr);
    EXPECT_EQ(idle_state->parent_state, "Active");

    const auto* processing_state = model.find_state("Processing");
    ASSERT_NE(processing_state, nullptr);
    EXPECT_EQ(processing_state->parent_state, "Active");

    const auto validation = FsmValidator::validate(model);
    EXPECT_TRUE(validation.is_valid);
}

/**
 * @brief Verify Mermaid composite state parsing and runtime initialization.
 * @scenario Parse Mermaid diagram declaring nested sub-states.
 * @expected Composite hierarchy formed and machine initializes in initial sub-state.
 */
TEST(HfsmHierarchy, MermaidCompositeState_NestedHierarchy_ParsedCorrectly) {
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
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    EXPECT_EQ(model.initial_state, "Session");

    const auto* session_state = model.find_state("Session");
    ASSERT_NE(session_state, nullptr);
    EXPECT_TRUE(session_state->is_composite);
    EXPECT_EQ(session_state->initial_sub_state, "Connected");

    const auto* conn_state = model.find_state("Connected");
    ASSERT_NE(conn_state, nullptr);
    EXPECT_EQ(conn_state->parent_state, "Session");

    const auto validation = FsmValidator::validate(model);
    EXPECT_TRUE(validation.is_valid);
}

}  // namespace

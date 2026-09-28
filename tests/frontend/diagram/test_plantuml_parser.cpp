/**
 * @file test_plantuml_parser.cpp
 * @brief Unit test suite for the PlantUML state diagram parser and frontend dialect.
 */

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"

using namespace fsm::frontend::diagram;
using namespace fsm::frontend;
using namespace fsm::middleend::analysis;
using namespace fsm::middleend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify basic PlantUML syntax parsing and model validation.
 * @scenario Parse PlantUML with transitions, guards, actions, and initial state pointer.
 * @expected FsmIr elements accurately populated and FsmValidator passes without errors.
 */
TEST(PlantUmlParser, BasicDiagram_ParsedIntoValidFsmIr) {
    const std::string puml = R"(
    @startuml
    [*] --> Standby
    Standby -> Processing : StartTask [HasWork] / InitTask
    Processing -> Standby : StopTask / Cleanup
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    EXPECT_EQ(model.initial_state, "Standby");
    EXPECT_EQ(model.states.size(), 2u);
    EXPECT_EQ(model.signals.size(), 2u);
    EXPECT_EQ(model.guards.size(), 1u);
    EXPECT_EQ(model.actions.size(), 2u);
    EXPECT_EQ(model.transitions.size(), 2u);

    const auto validation = FsmValidator::validate(model);
    EXPECT_TRUE(validation.is_valid);
}

/**
 * @brief Verify PlantUML single-line and multi-line comment stripping and composite states.
 * @scenario Parse PlantUML containing single-line (' ...) and block (/ ... /') comments with nested states.
 * @expected Comments discarded, composite hierarchy parsed, and substate parent links established.
 */
TEST(PlantUmlParser, CommentsAndCompositeHierarchy_ParsedCorrectly) {
    const std::string puml = R"(
    @startuml
    ' Single line comment
    /' Multi-line
       block comment '/
    [*] --> Operational
    
    state Operational {
        [*] --> SelfCheck
        SelfCheck : Progress / Report
        SelfCheck --> Armed : CheckOk
    }
    
    Operational --> Standby : Disarm
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    EXPECT_EQ(model.initial_state, "Operational");
    ASSERT_NE(model.find_state("Operational"), nullptr);
    EXPECT_TRUE(model.find_state("Operational")->is_composite);
    EXPECT_EQ(model.find_state("SelfCheck")->parent_state, "Operational");
}

/**
 * @brief Verify FsmValidator detects undefined transition target states in PlantUML models.
 * @scenario Construct FsmIr with transition to a non-existent state `UnknownTarget`.
 * @expected FsmValidator reports error diagnostics and marks model invalid.
 */
TEST(PlantUmlParser, UndefinedTargetState_DetectedByValidator) {
    FsmIr model;
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.transitions.emplace_back("Idle", "UnknownTarget", "MyEvent", std::nullopt, std::nullopt, "");

    const auto validation = FsmValidator::validate(model);
    EXPECT_FALSE(validation.is_valid);
    EXPECT_FALSE(validation.errors.empty());
}

/**
 * @brief Verify PlantUML parser gracefully rejects empty input.
 * @scenario Feed empty string to PlantUmlParser.
 * @expected Parser returns false with an informative error message.
 */
TEST(PlantUmlParser, EmptyInput_GracefullyRejected) {
    PlantUmlParser puml_parser;
    FsmIr model;
    std::string err;

    EXPECT_FALSE(puml_parser.parse("", model, err));
}

/**
 * @brief Verify PlantUML parsing of entryPoint, exitPoint, time invariants, and transition priorities.
 * @scenario Parse PlantUML containing entryPoint and exitPoint pseudostates, time invariant stay limit, and transition
 * priorities.
 * @expected IR captures StateKind::EntryPoint, StateKind::ExitPoint, time_invariant, and explicit priorities.
 */
TEST(PlantUmlParser, EntryExitPointPriorityAndInvariant_CapturedInIr) {
    const std::string puml = R"(
    @startuml
    [*] --> Idle
    state ep <<entryPoint>>
    state xp <<exitPoint>>
    state Active {
        Active : invariant stay <= 100ms
    }
    Idle --> Active : (prio=5) EvGo [GuardOk] / ActGo
    Active --> xp : (priority=2) EvDone
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    const auto* ep = model.find_state("ep");
    ASSERT_NE(ep, nullptr);
    EXPECT_EQ(ep->kind, StateKind::EntryPoint);

    const auto* xp = model.find_state("xp");
    ASSERT_NE(xp, nullptr);
    EXPECT_EQ(xp->kind, StateKind::ExitPoint);

    const auto* active = model.find_state("Active");
    ASSERT_NE(active, nullptr);
    ASSERT_TRUE(active->time_invariant.has_value());
    EXPECT_EQ(*active->time_invariant, "stay <= 100ms");

    ASSERT_EQ(model.transitions.size(), 2u);
    EXPECT_EQ(model.transitions[0].priority, 5u);
    EXPECT_EQ(model.transitions[1].priority, 2u);
}

/**
 * @brief Verify PlantUML parsing of @fsm:port inline directives into FsmIr ports.
 * @scenario Parse PlantUML containing @fsm:port comments with range constraints and physical units.
 * @expected Inbound and outbound ports added to FsmIr with bounds and units populated.
 */
TEST(PlantUmlParser, PortDirectives_ParsedWithAttributesAndConstraints) {
    const std::string puml = R"(
    @startuml
    ' @fsm:port name=sensor_temp type=float dir=in min=-40.0 max=125.0 constraint="self >= -40.0 and self <= 125.0" unit="[degC]"
    ' @fsm:port name=valve_cmd type=float dir=out min=0.0 max=100.0 constraint="self >= 0.0 and self <= 100.0"
    [*] --> Off
    Off --> On : EvStart
    @enduml
    )";

    PlantUmlParser puml_parser;
    FsmIr puml_model;
    std::string err;
    ASSERT_TRUE(puml_parser.parse(puml, puml_model, err)) << err;

    ASSERT_EQ(puml_model.ports.size(), 2u);
    const auto* in_p = puml_model.find_port("sensor_temp");
    ASSERT_NE(in_p, nullptr);
    EXPECT_TRUE(in_p->is_in());
    EXPECT_DOUBLE_EQ(in_p->min_value.value_or(0.0), -40.0);
    EXPECT_DOUBLE_EQ(in_p->max_value.value_or(0.0), 125.0);

    const auto* out_p = puml_model.find_port("valve_cmd");
    ASSERT_NE(out_p, nullptr);
    EXPECT_TRUE(out_p->is_out());
    EXPECT_DOUBLE_EQ(out_p->min_value.value_or(0.0), 0.0);
    EXPECT_DOUBLE_EQ(out_p->max_value.value_or(0.0), 100.0);
}

}  // namespace

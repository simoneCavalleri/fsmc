/**
 * @file test_diagram_sidecar.cpp
 * @brief Unit test suite for the Diagram Sidecar Pattern combining diagram topology with companion manifests.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/frontend/common/companion_manifest_parser.hpp"
#include "fsm/frontend/diagram/diagram_contract_combiner.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"

using namespace fsm::frontend;
using namespace fsm::frontend::diagram;
using namespace fsm::middleend::analysis;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify Diagram Sidecar Pattern combining PlantUML with .fsm.yaml companion manifest.
 * @scenario Ingest a clean visual PlantUML diagram and combine with external .fsm.yaml companion manifest.
 * @expected Resulting EFSM model is enriched with typed I/O ports, variables, state invariants, and verification
 * properties.
 */
TEST(DiagramSidecar, PlantUmlTopology_CombinedWithYamlCompanionManifest) {
    const std::string puml_diagram = R"(
    @startuml
    [*] --> Idle
    Idle --> Active : Start
    Active --> ErrorState : Fail
    ErrorState --> Idle : Reset
    @enduml
    )";

    const std::string yaml_manifest = R"yaml(
fsm:
  package: Robotics
  name: AutonomousRover
  initial: Idle

ports:
  - name: battery_level
    type: float
    direction: in
    constraint: "self >= 0.0 and self <= 100.0"
  - name: drive_speed
    type: int32_t
    direction: out

variables:
  - name: obstacle_distance
    type: float
    initial: "100.0"
    unit: cm

signals:
  - name: Start
    attributes:
      - name: mission_id
        type: uint32_t

invariants:
  Active: "stay duration <= 60000ms"

properties:
  - name: SafetyNoFailWhenBatteryOk
    formula: "G (battery_level > 20.0 -> !state == ErrorState)"

requirements:
  - REQ-ROVER-001
  - REQ-ROVER-002
    )yaml";

    // 1. Parse diagram topology
    PlantUmlParser diagram_parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(diagram_parser.parse(puml_diagram, model, err)) << "Diagram error: " << err;
    EXPECT_EQ(model.states.size(), 3u);
    EXPECT_EQ(model.transitions.size(), 3u);
    EXPECT_TRUE(model.ports.empty());
    EXPECT_TRUE(model.variables.empty());

    // 2. Parse YAML manifest
    CompanionManifest manifest;
    ASSERT_TRUE(CompanionManifestParser::parse(yaml_manifest, manifest, err)) << "Manifest error: " << err;
    EXPECT_EQ(manifest.package_name, "Robotics");
    EXPECT_EQ(manifest.fsm_name, "AutonomousRover");
    EXPECT_EQ(manifest.ports.size(), 2u);
    EXPECT_EQ(manifest.variables.size(), 1u);
    EXPECT_EQ(manifest.signals.size(), 1u);
    EXPECT_EQ(manifest.properties.size(), 1u);
    EXPECT_EQ(manifest.requirements.size(), 2u);

    // 3. Combine topology with contract
    std::string combine_err;
    ASSERT_TRUE(DiagramContractCombiner::combine(model, manifest, combine_err)) << combine_err;

    // 4. Verify enriched model contract
    EXPECT_EQ(model.package, "Robotics");
    EXPECT_EQ(model.name, "AutonomousRover");
    EXPECT_EQ(model.initial_state, "Idle");

    // Ports
    ASSERT_EQ(model.ports.size(), 2u);
    EXPECT_EQ(model.ports[0].name, "battery_level");
    EXPECT_EQ(model.ports[0].type, "float");
    EXPECT_EQ(model.ports[0].direction, PortDirection::In);
    EXPECT_EQ(model.ports[0].constraint, "self >= 0.0 and self <= 100.0");
    EXPECT_EQ(model.ports[1].name, "drive_speed");
    EXPECT_EQ(model.ports[1].type, "int32_t");
    EXPECT_EQ(model.ports[1].direction, PortDirection::Out);

    // Variables
    ASSERT_EQ(model.variables.size(), 1u);
    EXPECT_EQ(model.variables[0].name, "obstacle_distance");
    EXPECT_EQ(model.variables[0].type, "float");
    EXPECT_EQ(model.variables[0].physical_unit, "cm");

    // Signal payload
    ASSERT_FALSE(model.signals.empty());
    auto* start_sig = model.find_signal("Start");
    ASSERT_NE(start_sig, nullptr);
    ASSERT_EQ(start_sig->attributes.size(), 1u);
    EXPECT_EQ(start_sig->attributes[0].name, "mission_id");

    // State invariants
    auto* active_state = model.find_state("Active");
    ASSERT_NE(active_state, nullptr);
    ASSERT_TRUE(active_state->time_invariant.has_value());
    EXPECT_EQ(*active_state->time_invariant, "stay duration <= 60000ms");

    // Formal verification properties
    ASSERT_EQ(model.properties.size(), 1u);
    EXPECT_EQ(model.properties[0].name, "SafetyNoFailWhenBatteryOk");
    EXPECT_TRUE(model.properties[0].ast.has_value());

    // Requirements
    ASSERT_EQ(model.satisfies_reqs.size(), 2u);
    EXPECT_EQ(model.satisfies_reqs[0], "REQ-ROVER-001");
    EXPECT_EQ(model.satisfies_reqs[1], "REQ-ROVER-002");

    // Model is structurally valid
    const auto validation = FsmValidator::validate(model);
    EXPECT_TRUE(validation.is_valid);
}

/**
 * @brief Verify Diagram Sidecar Pattern with Mermaid topology and JSON manifest.
 * @scenario Ingest Mermaid diagram and combine with companion manifest declaring ports and variables.
 * @expected Model enriched with ports and variables and requirements successfully checked.
 */
TEST(DiagramSidecar, MermaidTopology_CombinedWithJsonCompanionManifest) {
    const std::string mmd_diagram = R"(
    stateDiagram-v2
        [*] --> Disconnected
        Disconnected --> Connected : ConnectCmd
        Connected --> Disconnected : DisconnectCmd
    )";

    const std::string json_manifest = R"(
fsm:
  package: "Network"
  name: "TcpConnectionFsm"
ports:
  - name: "rx_bytes"
    type: "uint64_t"
    direction: "in"
  - name: "tx_bytes"
    type: "uint64_t"
    direction: "out"
variables:
  - name: "retry_counter"
    type: "uint32_t"
    initial: "0"
requirements:
  - "REQ-NET-010"
    )";

    MermaidParser diagram_parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(diagram_parser.parse(mmd_diagram, model, err)) << err;

    CompanionManifest manifest;
    ASSERT_TRUE(CompanionManifestParser::parse(json_manifest, manifest, err)) << err;

    std::string combine_err;
    ASSERT_TRUE(DiagramContractCombiner::combine(model, manifest, combine_err)) << combine_err;

    EXPECT_EQ(model.package, "Network");
    EXPECT_EQ(model.name, "TcpConnectionFsm");
    ASSERT_EQ(model.ports.size(), 2u);
    ASSERT_EQ(model.variables.size(), 1u);
    EXPECT_EQ(model.variables[0].name, "retry_counter");
    ASSERT_EQ(model.satisfies_reqs.size(), 1u);
    EXPECT_EQ(model.satisfies_reqs[0], "REQ-NET-010");
}

}  // namespace

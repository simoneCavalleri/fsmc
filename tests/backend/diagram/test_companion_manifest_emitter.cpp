/**
 * @file test_companion_manifest_emitter.cpp
 * @brief Unit test suite for companion manifest emission (YAML/JSON sidecar contracts).
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/diagram/companion_manifest_emitter.hpp"
#include "fsm/frontend/common/companion_manifest_parser.hpp"
#include "fsm/frontend/diagram/diagram_contract_combiner.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::backend::diagram;
using namespace fsm::frontend;
using namespace fsm::frontend::diagram;
using namespace fsm::ir;

namespace {

FsmIr build_test_fsm() {
    FsmIr model;
    model.package = "Robotics.Space";
    model.name = "LunarLanderFsm";
    model.initial_state = "Idle";
    model.satisfies_reqs = {"REQ-LAND-01", "REQ-LAND-02"};

    // States
    StateNode idle{"Idle", "Initial resting state"};
    idle.kind = StateKind::Atomic;
    idle.traceability_reqs = {"REQ-LAND-01"};

    StateNode descending{"Descending", "Descent phase"};
    descending.kind = StateKind::Atomic;
    descending.time_invariant = StateTimeInvariant{"stay_duration <= 5000ms"};
    descending.entry_actions.push_back(ActionSignature{"StartThrusters", "thrusters.fire()"});
    descending.exit_actions.push_back(ActionSignature{"CutThrusters", "thrusters.cut()"});

    model.states.push_back(std::move(idle));
    model.states.push_back(std::move(descending));

    // Transitions
    TransitionEdge t1;
    t1.source = "Idle";
    t1.target = "Descending";
    t1.event = "StartDescent";
    t1.transition_action = ActionSignature{"ArmSensors", "sensors.arm()"};
    model.transitions.push_back(std::move(t1));

    // Ports
    PortDefinition p_alt;
    p_alt.name = "altitude";
    p_alt.type = DataType{PrimitiveTypeKind::Float32};
    p_alt.direction = PortDirection::In;
    p_alt.min_value = 0.0;
    p_alt.max_value = 100000.0;
    p_alt.constraint = "self >= 0.0";
    model.ports.push_back(std::move(p_alt));

    PortDefinition p_thr;
    p_thr.name = "thrust_cmd";
    p_thr.type = DataType{PrimitiveTypeKind::Float32};
    p_thr.direction = PortDirection::Out;
    p_thr.min_value = 0.0;
    p_thr.max_value = 100.0;
    model.ports.push_back(std::move(p_thr));

    // Variables
    VariableDefinition v_fuel;
    v_fuel.name = "fuel_remaining";
    v_fuel.type = DataType{PrimitiveTypeKind::Float32};
    v_fuel.initial_value = "100.0";
    v_fuel.physical_unit = "kg";
    v_fuel.min_value = 0;
    v_fuel.max_value = 100;
    model.variables.push_back(std::move(v_fuel));

    // Signals
    SignalDefinition sig;
    sig.name = "TelemetryCmd";
    sig.attributes.push_back(SignalAttribute{"rate_hz", DataType{PrimitiveTypeKind::UInt32}, "10"});
    model.signals.push_back(std::move(sig));

    // Properties
    FormalProperty prop;
    prop.name = "SafetyAltitudePositive";
    prop.raw_formula = "G (altitude >= 0.0)";
    prop.traceability_req = "REQ-LAND-03";
    model.properties.push_back(std::move(prop));

    return model;
}

/**
 * @brief Verify companion manifest data extraction from full FsmIr model.
 * @scenario Build a complete FsmIr model and extract CompanionManifest structure.
 * @expected Manifest correctly populated with package, ports, variables, signals, invariants, properties, actions, and
 * requirements.
 */
TEST(CompanionManifestEmitter, ModelContract_ExtractedIntoManifest) {
    FsmIr model = build_test_fsm();
    CompanionManifest manifest = CompanionManifestEmitter::build_manifest(model);

    EXPECT_EQ(manifest.package_name, "Robotics.Space");
    EXPECT_EQ(manifest.fsm_name, "LunarLanderFsm");
    EXPECT_EQ(manifest.initial_state, "Idle");

    ASSERT_EQ(manifest.ports.size(), 2u);
    EXPECT_EQ(manifest.ports[0].name, "altitude");
    EXPECT_EQ(manifest.ports[0].direction, "in");
    EXPECT_EQ(manifest.ports[1].name, "thrust_cmd");
    EXPECT_EQ(manifest.ports[1].direction, "out");

    ASSERT_EQ(manifest.variables.size(), 1u);
    EXPECT_EQ(manifest.variables[0].name, "fuel_remaining");
    EXPECT_EQ(manifest.variables[0].unit, "kg");

    ASSERT_EQ(manifest.signals.size(), 1u);
    EXPECT_EQ(manifest.signals[0].name, "TelemetryCmd");
    ASSERT_EQ(manifest.signals[0].attributes.size(), 1u);
    EXPECT_EQ(manifest.signals[0].attributes[0].name, "rate_hz");

    EXPECT_EQ(manifest.invariants.size(), 1u);
    EXPECT_EQ(manifest.invariants["Descending"], "stay_duration <= 5000ms");

    ASSERT_EQ(manifest.properties.size(), 1u);
    EXPECT_EQ(manifest.properties[0].name, "SafetyAltitudePositive");
    EXPECT_EQ(manifest.properties[0].formula, "G (altitude >= 0.0)");

    // Actions extracted from entry, exit, transition
    EXPECT_GE(manifest.actions.size(), 3u);

    // Requirements consolidated
    EXPECT_GE(manifest.requirements.size(), 3u);
}

/**
 * @brief Verify YAML companion manifest serialization and deserialization roundtrip.
 * @scenario Emit FsmIr to YAML sidecar string and re-parse into CompanionManifest.
 * @expected Deserialized manifest matches original ports, variables, signals, and invariants without loss.
 */
TEST(CompanionManifestEmitter, YamlSidecar_RoundtrippedLosslessly) {
    FsmIr model = build_test_fsm();
    std::string yaml_output = CompanionManifestEmitter::emit_yaml(model);
    EXPECT_FALSE(yaml_output.empty());
    EXPECT_NE(yaml_output.find("LunarLanderFsm"), std::string::npos);
    EXPECT_NE(yaml_output.find("altitude"), std::string::npos);

    CompanionManifest roundtrip_manifest;
    std::string err;
    ASSERT_TRUE(CompanionManifestParser::parse(yaml_output, roundtrip_manifest, err)) << err;

    EXPECT_EQ(roundtrip_manifest.package_name, "Robotics.Space");
    EXPECT_EQ(roundtrip_manifest.fsm_name, "LunarLanderFsm");
    EXPECT_EQ(roundtrip_manifest.initial_state, "Idle");
    ASSERT_EQ(roundtrip_manifest.ports.size(), 2u);
    EXPECT_EQ(roundtrip_manifest.ports[0].name, "altitude");
    EXPECT_EQ(roundtrip_manifest.ports[1].name, "thrust_cmd");
    ASSERT_EQ(roundtrip_manifest.variables.size(), 1u);
    EXPECT_EQ(roundtrip_manifest.variables[0].name, "fuel_remaining");
    ASSERT_EQ(roundtrip_manifest.signals.size(), 1u);
    EXPECT_EQ(roundtrip_manifest.signals[0].name, "TelemetryCmd");
    EXPECT_EQ(roundtrip_manifest.invariants["Descending"], "stay_duration <= 5000ms");
    ASSERT_EQ(roundtrip_manifest.properties.size(), 1u);
    EXPECT_EQ(roundtrip_manifest.properties[0].name, "SafetyAltitudePositive");
}

/**
 * @brief Verify JSON companion manifest serialization and deserialization roundtrip.
 * @scenario Emit FsmIr to JSON sidecar string and re-parse into CompanionManifest.
 * @expected Deserialized manifest matches original metadata, ports, and variables.
 */
TEST(CompanionManifestEmitter, JsonSidecar_RoundtrippedLosslessly) {
    FsmIr model = build_test_fsm();
    std::string json_output = CompanionManifestEmitter::emit_json(model);
    EXPECT_FALSE(json_output.empty());
    EXPECT_NE(json_output.find("\"LunarLanderFsm\""), std::string::npos);

    CompanionManifest roundtrip_manifest;
    std::string err;
    ASSERT_TRUE(CompanionManifestParser::parse(json_output, roundtrip_manifest, err)) << err;

    EXPECT_EQ(roundtrip_manifest.package_name, "Robotics.Space");
    EXPECT_EQ(roundtrip_manifest.fsm_name, "LunarLanderFsm");
    EXPECT_EQ(roundtrip_manifest.initial_state, "Idle");
    ASSERT_EQ(roundtrip_manifest.ports.size(), 2u);
    EXPECT_EQ(roundtrip_manifest.ports[0].name, "altitude");
    EXPECT_EQ(roundtrip_manifest.ports[1].name, "thrust_cmd");
    ASSERT_EQ(roundtrip_manifest.variables.size(), 1u);
    EXPECT_EQ(roundtrip_manifest.variables[0].name, "fuel_remaining");
}

/**
 * @brief Verify sidecar roundtrip with bare diagram topology and YAML manifest recombiner.
 * @scenario Emit YAML sidecar from complete model, parse bare PlantUML diagram topology, and recombine.
 * @expected Recombined model recovers ports, variables, and properties from sidecar.
 */
TEST(CompanionManifestEmitter, BareTopologyAndSidecar_RecombinedIntoCompleteModel) {
    FsmIr original_model = build_test_fsm();
    std::string yaml_sidecar = CompanionManifestEmitter::emit_yaml(original_model);

    // Bare PlantUML topology
    const std::string puml = R"(
    @startuml
    [*] --> Idle
    Idle --> Descending : StartDescent
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr bare_model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, bare_model, err)) << err;
    EXPECT_TRUE(bare_model.ports.empty());
    EXPECT_TRUE(bare_model.variables.empty());

    CompanionManifest manifest;
    ASSERT_TRUE(CompanionManifestParser::parse(yaml_sidecar, manifest, err)) << err;
    std::string combine_err;
    ASSERT_TRUE(DiagramContractCombiner::combine(bare_model, manifest, combine_err)) << combine_err;

    EXPECT_EQ(bare_model.package, "Robotics.Space");
    EXPECT_EQ(bare_model.name, "LunarLanderFsm");
    ASSERT_EQ(bare_model.ports.size(), 2u);
    ASSERT_EQ(bare_model.variables.size(), 1u);
    EXPECT_EQ(bare_model.variables[0].name, "fuel_remaining");
    ASSERT_EQ(bare_model.properties.size(), 1u);
    EXPECT_EQ(bare_model.properties[0].name, "SafetyAltitudePositive");
}

}  // namespace

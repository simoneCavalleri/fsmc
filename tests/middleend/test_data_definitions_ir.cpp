#include <gtest/gtest.h>

#include <optional>
#include <string>

// Test individual granular headers are self-contained
#include "fsm/ir/enum_definition.hpp"
#include "fsm/ir/struct_definition.hpp"

// Test umbrella aggregator header and serializers
#include "fsm/backend/diagram/json_serializer.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify EnumDefinition and EnumLiteral creation, query methods, and value semantics.
 *
 * Scenario:
 * - Instantiate EnumLiteral with default and explicit numeric values.
 * - Create EnumDefinition with custom underlying integer type and literals.
 * - Verify lookup methods has_literal, find_literal, find_literal_mut, and add_literal.
 * - Validate structural equality via operator==.
 */
TEST(DataDefinitionsIrTest, EnumDefinitionAndLiterals) {
    // 1. Literal construction
    EnumLiteral lit_idle("Idle", 0, "Idle quiescent state");
    EXPECT_EQ(lit_idle.name, "Idle");
    ASSERT_TRUE(lit_idle.value.has_value());
    EXPECT_EQ(*lit_idle.value, 0);
    EXPECT_EQ(lit_idle.description, "Idle quiescent state");

    EnumLiteral lit_active("Active");
    EXPECT_EQ(lit_active.name, "Active");
    EXPECT_FALSE(lit_active.value.has_value());

    // 2. Definition construction
    EnumDefinition enum_def("FmsOperatingMode", "uint8_t", "Flight Management System operating modes");
    EXPECT_EQ(enum_def.name, "FmsOperatingMode");
    EXPECT_EQ(enum_def.underlying_type, "uint8_t");
    EXPECT_EQ(enum_def.description, "Flight Management System operating modes");
    EXPECT_TRUE(enum_def.literals.empty());

    // 3. Adding literals & lookup
    enum_def.add_literal(lit_idle);
    enum_def.add_literal(lit_active);
    enum_def.add_literal(EnumLiteral("Degraded", 2, "Fault detected mode"));

    EXPECT_EQ(enum_def.literals.size(), 3u);
    EXPECT_TRUE(enum_def.has_literal("Idle"));
    EXPECT_TRUE(enum_def.has_literal("Active"));
    EXPECT_TRUE(enum_def.has_literal("Degraded"));
    EXPECT_FALSE(enum_def.has_literal("Emergency"));

    const auto* found_idle = enum_def.find_literal("Idle");
    ASSERT_NE(found_idle, nullptr);
    EXPECT_EQ(found_idle->name, "Idle");
    EXPECT_EQ(*found_idle->value, 0);

    auto* mut_degraded = enum_def.find_literal_mut("Degraded");
    ASSERT_NE(mut_degraded, nullptr);
    mut_degraded->value = 10;
    EXPECT_EQ(*enum_def.find_literal("Degraded")->value, 10);

    // Overwriting existing literal
    enum_def.add_literal(EnumLiteral("Idle", 100, "Updated idle"));
    EXPECT_EQ(enum_def.literals.size(), 3u);
    EXPECT_EQ(*enum_def.find_literal("Idle")->value, 100);
    EXPECT_EQ(enum_def.find_literal("Idle")->description, "Updated idle");

    // 4. Equality operator
    EnumDefinition copy_def = enum_def;
    EXPECT_EQ(enum_def, copy_def);
    copy_def.underlying_type = "uint16_t";
    EXPECT_NE(enum_def, copy_def);
}

/**
 * @brief Test Intent: Verify StructDefinition and StructField attributes, ISQ units, domain contracts, and equality.
 *
 * Scenario:
 * - Instantiate StructField with type, default initializer, ISQ physical units, and min/max contracts.
 * - Create StructDefinition for both composite structs and value datatypes (is_datatype).
 * - Verify field lookup, mutation, and structural equality via operator==.
 */
TEST(DataDefinitionsIrTest, StructDefinitionAndFields) {
    // 1. StructField construction with domain contracts and units
    StructField f_alt("targetAltitude_ft", "float", "10000.0f", "[ft]", 0.0, 60000.0, "Target flight plan altitude");
    EXPECT_EQ(f_alt.name, "targetAltitude_ft");
    EXPECT_EQ(f_alt.type, "float");
    EXPECT_EQ(f_alt.default_value, "10000.0f");
    ASSERT_TRUE(f_alt.physical_unit.has_value());
    EXPECT_EQ(*f_alt.physical_unit, "[ft]");
    ASSERT_TRUE(f_alt.min_value.has_value());
    EXPECT_DOUBLE_EQ(*f_alt.min_value, 0.0);
    ASSERT_TRUE(f_alt.max_value.has_value());
    EXPECT_DOUBLE_EQ(*f_alt.max_value, 60000.0);
    EXPECT_EQ(f_alt.description, "Target flight plan altitude");

    // 2. StructDefinition construction
    StructDefinition waypoint_def("FlightPlanWaypoint", false, "Avionics waypoint definition");
    EXPECT_EQ(waypoint_def.name, "FlightPlanWaypoint");
    EXPECT_FALSE(waypoint_def.is_datatype);
    EXPECT_EQ(waypoint_def.description, "Avionics waypoint definition");

    // 3. Adding fields and querying
    waypoint_def.add_field(StructField("waypointId", "uint32_t", "0"));
    waypoint_def.add_field(f_alt);
    waypoint_def.add_field(StructField("isActive", "bool", "false"));

    EXPECT_EQ(waypoint_def.fields.size(), 3u);
    EXPECT_TRUE(waypoint_def.has_field("waypointId"));
    EXPECT_TRUE(waypoint_def.has_field("targetAltitude_ft"));
    EXPECT_TRUE(waypoint_def.has_field("isActive"));
    EXPECT_FALSE(waypoint_def.has_field("speed_kts"));

    const auto* found_alt = waypoint_def.find_field("targetAltitude_ft");
    ASSERT_NE(found_alt, nullptr);
    EXPECT_EQ(found_alt->type, "float");

    auto* mut_id = waypoint_def.find_field_mut("waypointId");
    ASSERT_NE(mut_id, nullptr);
    mut_id->default_value = "1";
    EXPECT_EQ(waypoint_def.find_field("waypointId")->default_value, "1");

    // Overwriting field
    waypoint_def.add_field(StructField("isActive", "bool", "true"));
    EXPECT_EQ(waypoint_def.fields.size(), 3u);
    EXPECT_EQ(waypoint_def.find_field("isActive")->default_value, "true");

    // 4. Value Datatype distinction
    StructDefinition data_type("AirspeedMeasurement", true, "Value datatype");
    EXPECT_TRUE(data_type.is_datatype);

    // 5. Equality operator
    StructDefinition copy_def = waypoint_def;
    EXPECT_EQ(waypoint_def, copy_def);
    copy_def.is_datatype = true;
    EXPECT_NE(waypoint_def, copy_def);
}

/**
 * @brief Test Intent: Verify FsmIr metamodel container integration, lookup methods, and canonical sorting.
 *
 * Scenario:
 * - Register multiple enums and structs inside FsmIr.
 * - Validate find_enum, find_enum_mut, find_struct, and find_struct_mut.
 * - Call canonicalize() and verify deterministic alphabetical sorting for enums and structs.
 * - Verify FsmIr::operator== includes enums and structs in equality checks.
 */
TEST(DataDefinitionsIrTest, FsmIrIntegration) {
    FsmIr ir;
    ir.name = "FlightControlUnit";
    ir.ns = "avionics";

    // Add enums in non-alphabetical order
    EnumDefinition enum_b("LateralMode", "uint8_t");
    enum_b.add_literal(EnumLiteral("RollHold"));
    enum_b.add_literal(EnumLiteral("NavTrack"));

    EnumDefinition enum_a("AutoPilotState", "uint8_t");
    enum_a.add_literal(EnumLiteral("Disengaged"));
    enum_a.add_literal(EnumLiteral("Engaged"));

    ir.add_enum(enum_b);
    ir.add_enum(enum_a);

    // Add structs in non-alphabetical order
    StructDefinition struct_z("Waypoint", false);
    struct_z.add_field(StructField("lat", "double"));

    StructDefinition struct_a("AirData", true);
    struct_a.add_field(StructField("calibratedAirspeed", "float"));

    ir.add_struct(struct_z);
    ir.add_struct(struct_a);

    // Query lookups
    ASSERT_NE(ir.find_enum("LateralMode"), nullptr);
    ASSERT_NE(ir.find_enum("AutoPilotState"), nullptr);
    EXPECT_EQ(ir.find_enum("UnknownEnum"), nullptr);

    ASSERT_NE(ir.find_struct("Waypoint"), nullptr);
    ASSERT_NE(ir.find_struct("AirData"), nullptr);
    EXPECT_EQ(ir.find_struct("UnknownStruct"), nullptr);

    // Mutating lookups
    auto* mut_lat = ir.find_enum_mut("LateralMode");
    ASSERT_NE(mut_lat, nullptr);
    mut_lat->description = "Lateral guidance mode";
    EXPECT_EQ(ir.find_enum("LateralMode")->description, "Lateral guidance mode");

    // Canonicalization
    ir.canonicalize();

    ASSERT_EQ(ir.enums.size(), 2u);
    EXPECT_EQ(ir.enums[0].name, "AutoPilotState");
    EXPECT_EQ(ir.enums[1].name, "LateralMode");

    ASSERT_EQ(ir.structs.size(), 2u);
    EXPECT_EQ(ir.structs[0].name, "AirData");
    EXPECT_EQ(ir.structs[1].name, "Waypoint");

    // Structural equality check on FsmIr
    FsmIr ir_copy = ir;
    EXPECT_EQ(ir, ir_copy);

    ir_copy.enums[0].underlying_type = "uint32_t";
    EXPECT_NE(ir, ir_copy);
}

/**
 * @brief Test Intent: Verify lossless JSON IR serialization for user-defined enums and struct definitions.
 *
 * Scenario:
 * - Build FsmIr with complete enum and struct definitions including contracts, units, and descriptions.
 * - Serialize to JSON using FsmIrSerializer::serialize_json.
 * - Verify presence and structure of "enums" and "structs" JSON arrays.
 */
TEST(DataDefinitionsIrTest, FsmIrJsonSerializationRoundtrip) {
    FsmIr ir;
    ir.name = "AvionicsMissionComputer";
    ir.ns = "fms";

    // Enum
    EnumDefinition mode("NavMode", "uint8_t", "Navigation operating modes");
    mode.add_literal(EnumLiteral("Manual", 0, "Manual pilot control"));
    mode.add_literal(EnumLiteral("Auto", 1, "Autonomous waypoint tracking"));
    ir.add_enum(mode);

    // Struct
    StructDefinition sensor_data("SensorTelemetry", false, "Telemetry packet");
    sensor_data.add_field(
        StructField("baroAltitude_m", "float", "0.0f", "[m]", -500.0, 30000.0, "Barometric altitude"));
    sensor_data.add_field(StructField("battery_pct", "uint8_t", "100", "[%]", 0.0, 100.0, "Remaining battery"));
    ir.add_struct(sensor_data);

    ir.canonicalize();

    std::string json = FsmIrSerializer::serialize_json(ir);

    // Verify Enum serialization
    EXPECT_NE(json.find("\"enums\": ["), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"NavMode\""), std::string::npos);
    EXPECT_NE(json.find("\"underlying_type\": \"uint8_t\""), std::string::npos);
    EXPECT_NE(json.find("\"description\": \"Navigation operating modes\""), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"Manual\""), std::string::npos);
    EXPECT_NE(json.find("\"value\": 0"), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"Auto\""), std::string::npos);
    EXPECT_NE(json.find("\"value\": 1"), std::string::npos);

    // Verify Struct serialization
    EXPECT_NE(json.find("\"structs\": ["), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"SensorTelemetry\""), std::string::npos);
    EXPECT_NE(json.find("\"is_datatype\": false"), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"baroAltitude_m\""), std::string::npos);
    EXPECT_NE(json.find("\"type\": \"float\""), std::string::npos);
    EXPECT_NE(json.find("\"default_value\": \"0.0f\""), std::string::npos);
    EXPECT_NE(json.find("\"physical_unit\": \"[m]\""), std::string::npos);
    EXPECT_NE(json.find("\"min_value\": -500"), std::string::npos);
    EXPECT_NE(json.find("\"max_value\": 30000"), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"battery_pct\""), std::string::npos);
    EXPECT_NE(json.find("\"physical_unit\": \"[%]\""), std::string::npos);
}

/**
 * @brief Test Intent: Verify diagram JsonSerializer emission of enums and structs for Studio Playground export.
 *
 * Scenario:
 * - Serialize FsmIr with enums and structs using JsonSerializer::serialize.
 * - Verify enums and structs sections are rendered properly in diagram JSON.
 */
TEST(DataDefinitionsIrTest, DiagramJsonSerializerExport) {
    FsmIr model;
    model.name = "DiagramExportMachine";

    EnumDefinition en("EngineState", "uint8_t");
    en.add_literal(EnumLiteral("Off", 0));
    en.add_literal(EnumLiteral("Running", 1));
    model.add_enum(en);

    StructDefinition st("EngineTelemetry", false);
    st.add_field(StructField("rpm", "uint32_t", "0"));
    model.add_struct(st);

    std::string json = JsonSerializer::serialize(model);

    EXPECT_NE(json.find("\"enums\": ["), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"EngineState\""), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"Off\""), std::string::npos);
    EXPECT_NE(json.find("\"value\": 0"), std::string::npos);

    EXPECT_NE(json.find("\"structs\": ["), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"EngineTelemetry\""), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"rpm\""), std::string::npos);
}

}  // namespace

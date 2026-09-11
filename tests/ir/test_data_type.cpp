/**
 * @file test_data_type.cpp
 * @brief Unit tests for DataType target-agnostic type system, classification, parsing, and multi-language lowering.
 */

#include <gtest/gtest.h>

#include <sstream>

#include "fsm/ir/data_type.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/port_definition.hpp"
#include "fsm/ir/signal_definition.hpp"
#include "fsm/ir/struct_definition.hpp"
#include "fsm/ir/variable_definition.hpp"

using namespace fsm::ir;

/**
 * @brief Verify DataType primitive factories, kind classifications, and bit-width calculations.
 * @scenario Instantiate primitive types (boolean, uint32, int16, float64, string) via static factory helpers.
 * @expected Classification predicates (is_primitive, is_integer, etc.) and bit widths evaluate correctly.
 */
TEST(DataType, PrimitiveFactories_ClassifiesKindsAndBitWidths) {
    auto b = DataType::boolean();
    EXPECT_TRUE(b.is_primitive());
    EXPECT_TRUE(b.is_boolean());
    EXPECT_FALSE(b.is_integer());
    EXPECT_EQ(b.bit_width(), 8);

    auto u32 = DataType::uint32();
    EXPECT_TRUE(u32.is_primitive());
    EXPECT_TRUE(u32.is_integer());
    EXPECT_TRUE(u32.is_unsigned_integer());
    EXPECT_FALSE(u32.is_signed_integer());
    EXPECT_EQ(u32.bit_width(), 32);

    auto i16 = DataType::int16();
    EXPECT_TRUE(i16.is_primitive());
    EXPECT_TRUE(i16.is_integer());
    EXPECT_TRUE(i16.is_signed_integer());
    EXPECT_FALSE(i16.is_unsigned_integer());
    EXPECT_EQ(i16.bit_width(), 16);

    auto f64 = DataType::float64();
    EXPECT_TRUE(f64.is_primitive());
    EXPECT_TRUE(f64.is_floating_point());
    EXPECT_FALSE(f64.is_integer());
    EXPECT_EQ(f64.bit_width(), 64);

    auto str = DataType::string();
    EXPECT_TRUE(str.is_primitive());
    EXPECT_TRUE(str.is_string());
}

/**
 * @brief Verify DataType user-defined types (enumeration, structure, custom handle).
 * @scenario Create enum 'SystemMode', struct 'SensorPacket', and custom handle 'CustomHandle'.
 * @expected Is_primitive is false, specific type classification is true, and custom name matches.
 */
TEST(DataType, CustomEnumAndStructTypes_PreservesTypenameAndKind) {
    auto en = DataType::enumeration("SystemMode");
    EXPECT_FALSE(en.is_primitive());
    EXPECT_TRUE(en.is_enum());
    EXPECT_FALSE(en.is_struct());
    EXPECT_EQ(en.custom_name(), "SystemMode");

    auto st = DataType::structure("SensorPacket");
    EXPECT_FALSE(st.is_primitive());
    EXPECT_FALSE(st.is_enum());
    EXPECT_TRUE(st.is_struct());
    EXPECT_EQ(st.custom_name(), "SensorPacket");

    auto cu = DataType::custom("CustomHandle");
    EXPECT_FALSE(cu.is_primitive());
    EXPECT_TRUE(cu.is_custom());
    EXPECT_EQ(cu.custom_name(), "CustomHandle");
}

/**
 * @brief Verify DataType::from_string parsing across SysML v2 / KerML, C++, and custom declarations.
 * @scenario Strings representing primitive and complex types across language syntaxes.
 * @expected Types correctly deserialize into normalized canonical DataType representations.
 */
TEST(DataType, StringTypeRepresentation_ParsesSysmlAndCppTypenames) {
    // SysML v2 / KerML types
    EXPECT_EQ(DataType::from_string("Boolean"), DataType::boolean());
    EXPECT_EQ(DataType::from_string("Integer"), DataType::uint32());
    EXPECT_EQ(DataType::from_string("Natural"), DataType::uint32());
    EXPECT_EQ(DataType::from_string("Positive"), DataType::uint32());
    EXPECT_EQ(DataType::from_string("Real"), DataType::float32());
    EXPECT_EQ(DataType::from_string("Double"), DataType::float64());
    EXPECT_EQ(DataType::from_string("String"), DataType::string());
    EXPECT_EQ(DataType::from_string("ScalarValues::Integer"), DataType::uint32());

    // C++ types
    EXPECT_EQ(DataType::from_string("bool"), DataType::boolean());
    EXPECT_EQ(DataType::from_string("uint32_t"), DataType::uint32());
    EXPECT_EQ(DataType::from_string("int32_t"), DataType::int32());
    EXPECT_EQ(DataType::from_string("uint8_t"), DataType::uint8());
    EXPECT_EQ(DataType::from_string("float"), DataType::float32());
    EXPECT_EQ(DataType::from_string("double"), DataType::float64());
    EXPECT_EQ(DataType::from_string("std::string"), DataType::string());

    // Enum / struct prefix
    EXPECT_EQ(DataType::from_string("enum FlightState"), DataType::enumeration("FlightState"));
    EXPECT_EQ(DataType::from_string("struct Waypoint"), DataType::structure("Waypoint"));
}

/**
 * @brief Verify multi-target backend type lowering for C++, SysML v2, Rust, and nuXmv / SMV.
 * @scenario DataType objects for uint32, boolean, float32, and custom user types.
 * @expected Canonical target-specific type strings generated accurately for each compiler backend.
 */
TEST(DataType, TargetAgnosticTypeLowering_EmitsCppSysmlRustAndSmvTypes) {
    auto u32 = DataType::uint32();
    EXPECT_EQ(u32.to_canonical_string(), "uint32");
    EXPECT_EQ(u32.to_cpp_type(), "uint32_t");
    EXPECT_EQ(u32.to_sysml_type(), "Integer");
    EXPECT_EQ(u32.to_rust_type(), "u32");
    EXPECT_EQ(u32.to_smv_type(0, 50), "0..50");

    auto b = DataType::boolean();
    EXPECT_EQ(b.to_canonical_string(), "bool");
    EXPECT_EQ(b.to_cpp_type(), "bool");
    EXPECT_EQ(b.to_sysml_type(), "Boolean");
    EXPECT_EQ(b.to_rust_type(), "bool");
    EXPECT_EQ(b.to_smv_type(), "boolean");

    auto f32 = DataType::float32();
    EXPECT_EQ(f32.to_canonical_string(), "float32");
    EXPECT_EQ(f32.to_cpp_type(), "float");
    EXPECT_EQ(f32.to_sysml_type(), "Real");
    EXPECT_EQ(f32.to_rust_type(), "f32");

    auto custom = DataType::custom("UserType");
    EXPECT_EQ(custom.to_canonical_string(), "UserType");
    EXPECT_EQ(custom.to_cpp_type(), "UserType");
    EXPECT_EQ(custom.to_sysml_type(), "UserType");
    EXPECT_EQ(custom.to_rust_type(), "UserType");
}

/**
 * @brief Verify seamless integration of DataType across VariableDefinition, PortDefinition, StructField, and
 * SignalAttribute.
 * @scenario Entity definitions initialized with native strings.
 * @expected Types are implicitly or explicitly mapped to strongly-typed DataType instances.
 */
TEST(DataType, MetamodelEntitiesIntegration_AdaptsVariablePortAndSignalTypes) {
    VariableDefinition var("count", "uint32_t", "10");
    EXPECT_EQ(var.type, DataType::uint32());
    EXPECT_EQ(var.type.to_canonical_string(), "uint32");
    EXPECT_EQ(var.type.to_rust_type(), "u32");
    EXPECT_EQ(var.type, "uint32_t");

    PortDefinition port("altitude", "float", PortDirection::In, 0.0, 50000.0);
    EXPECT_EQ(port.type, DataType::float32());
    EXPECT_EQ(port.type.to_sysml_type(), "Real");
    EXPECT_EQ(port.type, "float");

    StructField field("sensor_val", "double");
    EXPECT_EQ(field.type, DataType::float64());
    EXPECT_EQ(field.type, "double");

    SignalAttribute attr("msg", "string");
    EXPECT_EQ(attr.type, DataType::string());
    EXPECT_EQ(attr.type, "string");
}

/**
 * @brief Verify GuardAstNode multi-language boolean syntax lowering.
 * @scenario AST representing expression 'is_ready && !timeout'.
 * @expected Emits 'is_ready && !timeout' for C++/Rust, 'is_ready and not timeout' for SysML, and 'is_ready & !timeout'
 * for SMV.
 */
TEST(GuardAstNode, TargetAgnosticBooleanSyntax_EmitsCppSysmlSmvAndRustExpressions) {
    GuardAstNode node_a("is_ready");
    GuardAstNode node_b("timeout");
    GuardAstNode not_b(GuardOp::Not, {node_b});
    GuardAstNode and_node(GuardOp::And, {node_a, not_b});

    // ISO C++
    EXPECT_EQ(and_node.to_cpp(), "is_ready && !timeout");

    // SysML v2 / KerML
    EXPECT_EQ(and_node.to_sysml(), "is_ready and not timeout");

    // nuXmv / SMV
    EXPECT_EQ(and_node.to_smv(), "is_ready & !timeout");

    // Rust
    EXPECT_EQ(and_node.to_rust(), "is_ready && !timeout");
}

/**
 * @brief Verify GuardModel retains both original and normalized algebraic expressions.
 * @scenario Construct GuardModel with raw SysML 'a and b' and normalized 'a && b'.
 * @expected Normalized expression is preserved in optional field.
 */
TEST(GuardModel, NormalizedAlgebraicExpression_PreservesCanonicalString) {
    GuardModel gm("Guard1", "desc", "a and b", "a && b");
    EXPECT_TRUE(gm.normalized_expression.has_value());
    EXPECT_EQ(*gm.normalized_expression, "a && b");
}

/**
 * @brief Verify TypeClassification string serialization and stream output operator.
 * @scenario Query type_classification_to_string and stream operator << on DataType.
 * @expected Formatted strings match expected canonical tokens.
 */
TEST(DataType, TypeClassificationStringFormatting_SerializesToStream) {
    EXPECT_EQ(type_classification_to_string(TypeClassification::Primitive), "primitive");
    EXPECT_EQ(type_classification_to_string(TypeClassification::Enum), "enum");
    EXPECT_EQ(type_classification_to_string(TypeClassification::Struct), "struct");
    EXPECT_EQ(type_classification_to_string(TypeClassification::Custom), "custom");

    std::ostringstream ss;
    ss << DataType::uint32();
    EXPECT_EQ(ss.str(), "uint32");
}

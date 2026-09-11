/**
 * @file test_directive_parser.cpp
 * @brief Unit tests for front-end directive parser (@fsm:state, @fsm:defer, @fsm:signal, @fsm:port, @fsm:enum,
 * @fsm:struct).
 */

#include <gtest/gtest.h>

#include "fsm/frontend/directive/directive_parser.hpp"

using namespace fsm::frontend::directive;
using namespace fsm::frontend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify '@fsm:state' directive parsing for traceability requirements and history metadata.
 * @scenario Parse line "' @fsm:state history=deep satisfies=[\"REQ-1\", \"SAFETY-04\"] do_activity=\"sensor_worker\"".
 * @expected State kind is set to DeepHistory, traceability requirements vector is populated, and do_activity is set.
 */
TEST(DirectiveParser, StateDirective_ParsesTraceabilityAndActivities) {
    std::string line = "' @fsm:state history=deep satisfies=[\"REQ-1\", \"SAFETY-04\"] do_activity=\"sensor_worker\"";
    EXPECT_TRUE(DirectiveParser::is_directive(line));

    std::string body = DirectiveParser::extract_directive_body(line);
    EXPECT_NE(body.find("state"), std::string::npos);

    StateNode state;
    DirectiveParser::parse_state_directive(body, state);

    EXPECT_EQ(state.kind, StateKind::DeepHistory);
    ASSERT_TRUE(state.do_activity.has_value());
    EXPECT_EQ(*state.do_activity, "sensor_worker");
    ASSERT_EQ(state.traceability_reqs.size(), 2u);
    EXPECT_EQ(state.traceability_reqs[0], "REQ-1");
    EXPECT_EQ(state.traceability_reqs[1], "SAFETY-04");
}

/**
 * @brief Verify '@fsm:defer [...]' directive parsing for deferred events.
 * @scenario Parse line "%% @fsm:defer [EvSensorReady, EvAck, EvTimeout]".
 * @expected All 3 event identifiers are parsed into the state's deferred_events list.
 */
TEST(DirectiveParser, DeferDirective_ExtractsDeferredEventList) {
    std::string line = "%% @fsm:defer [EvSensorReady, EvAck, EvTimeout]";
    EXPECT_TRUE(DirectiveParser::is_directive(line));

    std::string body = DirectiveParser::extract_directive_body(line);
    StateNode state;
    DirectiveParser::parse_defer_directive(body, state);

    ASSERT_EQ(state.deferred_events.size(), 3u);
    EXPECT_EQ(state.deferred_events[0], "EvSensorReady");
    EXPECT_EQ(state.deferred_events[1], "EvAck");
    EXPECT_EQ(state.deferred_events[2], "EvTimeout");
}

/**
 * @brief Verify '@fsm:signal' directive parsing with payload attributes and validation expressions.
 * @scenario Directive declaring EvPacketRecv with uint32_t len, pointer ptr, and validation constraint.
 * @expected SignalDefinition object created with attributes, types, and validator strings.
 */
TEST(DirectiveParser, SignalDirective_ExtractsPayloadAttributesAndValidators) {
    std::string line =
        "' @fsm:signal EvPacketRecv{uint32_t len, const uint8_t* ptr} validator=\"len > 0 && ptr != nullptr\"";
    EXPECT_TRUE(DirectiveParser::is_directive(line));

    std::string body = DirectiveParser::extract_directive_body(line);
    auto sig = DirectiveParser::parse_signal_directive(body);
    ASSERT_TRUE(sig.has_value());

    EXPECT_EQ(sig->name, "EvPacketRecv");
    ASSERT_EQ(sig->attributes.size(), 2u);
    EXPECT_EQ(sig->attributes[0].name, "len");
    EXPECT_EQ(sig->attributes[0].type, "uint32_t");
    EXPECT_EQ(sig->attributes[1].name, "ptr");
    EXPECT_EQ(sig->attributes[1].type, "const uint8_t*");
    ASSERT_EQ(sig->validators.size(), 1u);
    EXPECT_EQ(sig->validators[0], "len > 0 && ptr != nullptr");
}

/**
 * @brief Verify '@fsm:trans' directive parsing for custom transition IDs, guard ASTs, and actions.
 * @scenario Parse directive specifying id="tr_001", guard_ast="ctx.is_valid(payload)",
 * action_sig="ctx.on_data(payload)".
 * @expected TransitionEdge metadata is populated with explicit ID, guard AST, and action signature.
 */
TEST(DirectiveParser, TransitionDirective_PopulatesGuardAstAndActionSignatures) {
    std::string line =
        "%% @fsm:trans id=\"tr_001\" guard_ast=\"ctx.is_valid(payload)\" action_sig=\"ctx.on_data(payload)\"";
    EXPECT_TRUE(DirectiveParser::is_directive(line));

    std::string body = DirectiveParser::extract_directive_body(line);
    TransitionEdge trans;
    DirectiveParser::parse_trans_directive(body, trans);

    EXPECT_EQ(trans.id, "tr_001");
    ASSERT_TRUE(trans.guard_ast.has_value());
    EXPECT_EQ(trans.guard_ast->to_string(), "ctx.is_valid(payload)");
    ASSERT_TRUE(trans.transition_action.has_value());
    EXPECT_EQ(trans.transition_action->name, "ctx.on_data(payload)");
}

/**
 * @brief Verify '@fsm:port' directive parsing with direction, numeric bounds, and constraint expression.
 * @scenario Directive declaring InPort 'sensor_val' bounded in [0.0, 100.0] with unit [degC] and description.
 * @expected PortDefinition object populated with direction In, numerical bounds, physical units, and description.
 */
TEST(DirectiveParser, PortDirective_ExtractsDirectionBoundsAndUnits) {
    std::string line =
        "' @fsm:port name=sensor_val type=float dir=in min=0.0 max=100.0 constraint=\"self >= 0.0 and self <= 100.0\" "
        "unit=\"[degC]\" desc=\"Primary sensor\"";
    EXPECT_TRUE(DirectiveParser::is_directive(line));

    std::string body = DirectiveParser::extract_directive_body(line);
    auto port = DirectiveParser::parse_port_directive(body);
    ASSERT_TRUE(port.has_value());

    EXPECT_EQ(port->name, "sensor_val");
    EXPECT_EQ(port->type, "float");
    EXPECT_TRUE(port->is_in());
    EXPECT_FALSE(port->is_out());
    EXPECT_DOUBLE_EQ(port->min_value.value_or(0.0), 0.0);
    EXPECT_DOUBLE_EQ(port->max_value.value_or(0.0), 100.0);
    EXPECT_EQ(port->constraint, "self >= 0.0 and self <= 100.0");
    ASSERT_TRUE(port->physical_unit.has_value());
    EXPECT_EQ(*port->physical_unit, "[degC]");
    EXPECT_EQ(port->description, "Primary sensor");
}

/**
 * @brief Verify '@fsm:enum' directive parsing and roundtrip serialization.
 * @scenario Enum directive with underlying type uint8_t and literals Standby=0, Armed=1, Active=2.
 * @expected EnumDefinition correctly parsed and serializes back to equivalent directive string.
 */
TEST(DirectiveParser, EnumDirective_RoundtripsSerializationFidelity) {
    std::string line =
        "' @fsm:enum name=OperatingMode type=uint8_t literals=[Standby=0, Armed=1, Active=2] desc=\"Operating modes\"";
    EXPECT_TRUE(DirectiveParser::is_directive(line));

    std::string body = DirectiveParser::extract_directive_body(line);
    auto en = DirectiveParser::parse_enum_directive(body);
    ASSERT_TRUE(en.has_value());

    EXPECT_EQ(en->name, "OperatingMode");
    EXPECT_EQ(en->underlying_type, "uint8_t");
    EXPECT_EQ(en->description, "Operating modes");
    ASSERT_EQ(en->literals.size(), 3u);
    EXPECT_EQ(en->literals[0].name, "Standby");
    EXPECT_EQ(en->literals[0].value, 0);
    EXPECT_EQ(en->literals[1].name, "Armed");
    EXPECT_EQ(en->literals[1].value, 1);
    EXPECT_EQ(en->literals[2].name, "Active");
    EXPECT_EQ(en->literals[2].value, 2);

    std::string formatted = DirectiveParser::format_enum_directive(*en);
    auto en2 = DirectiveParser::parse_enum_directive(formatted);
    ASSERT_TRUE(en2.has_value());
    EXPECT_EQ(*en, *en2);
}

/**
 * @brief Verify '@fsm:struct' directive parsing and roundtrip serialization.
 * @scenario Struct directive declaring Waypoint datatype with fields lat:float=0.0, lon:float=0.0, alt:int32=100.
 * @expected StructDefinition correctly parsed and serializes back to identical directive format.
 */
TEST(DirectiveParser, StructDirective_RoundtripsFieldDefinitionsFidelity) {
    std::string line =
        "// @fsm:struct name=Waypoint is_datatype=true fields=[lat:float=0.0, lon:float=0.0, alt:int32=100] "
        "desc=\"Waypoint record\"";
    EXPECT_TRUE(DirectiveParser::is_directive(line));

    std::string body = DirectiveParser::extract_directive_body(line);
    auto st = DirectiveParser::parse_struct_directive(body);
    ASSERT_TRUE(st.has_value());

    EXPECT_EQ(st->name, "Waypoint");
    EXPECT_TRUE(st->is_datatype);
    EXPECT_EQ(st->description, "Waypoint record");
    ASSERT_EQ(st->fields.size(), 3u);
    EXPECT_EQ(st->fields[0].name, "lat");
    EXPECT_EQ(st->fields[0].type, "float");
    EXPECT_EQ(st->fields[0].default_value, "0.0");
    EXPECT_EQ(st->fields[1].name, "lon");
    EXPECT_EQ(st->fields[1].type, "float");
    EXPECT_EQ(st->fields[1].default_value, "0.0");
    EXPECT_EQ(st->fields[2].name, "alt");
    EXPECT_EQ(st->fields[2].type, "int32");
    EXPECT_EQ(st->fields[2].default_value, "100");

    std::string formatted = DirectiveParser::format_struct_directive(*st);
    auto st2 = DirectiveParser::parse_struct_directive(formatted);
    ASSERT_TRUE(st2.has_value());
    EXPECT_EQ(*st, *st2);
}

}  // namespace

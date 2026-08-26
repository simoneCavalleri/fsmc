#include <gtest/gtest.h>

#include "fsm/frontend/directive_parser.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify `@fsm:state` directive parsing for traceability requirements and history metadata.
 *
 * Scenario:
 * - Parse `@fsm:state history=deep satisfies=["REQ-1", "SAFETY-04"] do_activity="sensor_worker"`.
 * - Verify state kind is DeepHistory, requirements array is populated, and do_activity is set.
 */
TEST(DirectiveParserTest, ParseStateDirective) {
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
 * @brief Test Intent: Verify `@fsm:defer [...]` directive parsing for deferred events.
 *
 * Scenario:
 * - Parse `%% @fsm:defer [EvSensorReady, EvAck, EvTimeout]`.
 * - Verify all 3 event identifiers are parsed into state deferred_events.
 */
TEST(DirectiveParserTest, ParseDeferDirective) {
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
 * @brief Test Intent: Verify `@fsm:signal` directive parsing with payload attributes and validation expressions.
 *
 * Scenario:
 * - Parse `' @fsm:signal EvPacketRecv{uint32_t len, const uint8_t* ptr} validator="len > 0 && ptr != nullptr"'`.
 * - Verify SignalDefinition attributes, types, and validator constraints are parsed.
 */
TEST(DirectiveParserTest, ParseSignalDirective) {
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
 * @brief Test Intent: Verify `@fsm:trans` directive parsing for custom transition IDs, guard ASTs, and actions.
 *
 * Scenario:
 * - Parse `%% @fsm:trans id="tr_001" guard_ast="ctx.is_valid(payload)" action_sig="ctx.on_data(payload)"`.
 * - Verify TransitionEdge metadata is populated.
 */
TEST(DirectiveParserTest, ParseTransDirective) {
    std::string line =
        "%% @fsm:trans id=\"tr_001\" guard_ast=\"ctx.is_valid(payload)\" action_sig=\"ctx.on_data(payload)\"";
    EXPECT_TRUE(DirectiveParser::is_directive(line));

    std::string body = DirectiveParser::extract_directive_body(line);
    TransitionEdge trans;
    DirectiveParser::parse_trans_directive(body, trans);

    EXPECT_EQ(trans.id, "tr_001");
    ASSERT_TRUE(trans.guard_ast.has_value());
    EXPECT_EQ(trans.guard_ast->to_string(), "ctx.is_valid(payload)");
    ASSERT_TRUE(trans.action_sig.has_value());
    EXPECT_EQ(trans.action_sig->name, "ctx.on_data(payload)");
}

}  // namespace

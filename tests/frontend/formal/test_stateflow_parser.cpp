#include <gtest/gtest.h>

#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/frontend/formal/stateflow_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify Stateflow XML model ingestion, state hierarchies, and transition labels.
 *
 * Scenario:
 * - Ingest Stateflow XML chart with states and transitions formatted as Event [Guard] / { Action }.
 * - Verify parsed state machine hierarchy, triggers, guards, and actions.
 */
TEST(StateflowParserTest, BasicChartIngestion) {
    const std::string stateflow_xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Stateflow>\n"
        "    <machine name=\"AutoCruiseSystem\">\n"
        "        <chart id=\"1\" name=\"CruiseController\">\n"
        "            <state id=\"10\" name=\"Off\"/>\n"
        "            <state id=\"20\" name=\"Active\">\n"
        "                <state id=\"21\" name=\"Accelerating\"/>\n"
        "                <state id=\"22\" name=\"Cruising\"/>\n"
        "                <transition src=\"Accelerating\" dst=\"Cruising\" labelString=\"SpeedReached [speed &gt;= "
        "targetSpeed] / { holdSpeed(); }\"/>\n"
        "            </state>\n"
        "            <transition src=\"Off\" dst=\"Active\" labelString=\"EvEngage [brakePedal == 0] / { "
        "engageClutch(); }\"/>\n"
        "            <transition src=\"Active\" dst=\"Off\" labelString=\"EvDisengage\"/>\n"
        "        </chart>\n"
        "    </machine>\n"
        "</Stateflow>\n";

    StateflowParser parser;
    FsmIr model;
    std::string err;
    bool ok = parser.parse(stateflow_xml, model, err);

    ASSERT_TRUE(ok) << "Stateflow parse error: " << err;
    EXPECT_EQ(model.name, "CruiseController");

    // Verify states
    ASSERT_NE(model.find_state("Off"), nullptr);
    ASSERT_NE(model.find_state("Active"), nullptr);
    ASSERT_NE(model.find_state("Accelerating"), nullptr);
    ASSERT_NE(model.find_state("Cruising"), nullptr);

    const auto* acc = model.find_state("Accelerating");
    EXPECT_EQ(acc->parent_state, "Active");

    // Verify transitions
    ASSERT_EQ(model.transitions.size(), 3u);

    // Verify transition with guard and action
    const auto& t_engage = model.transitions[1];
    EXPECT_EQ(t_engage.source, "Off");
    EXPECT_EQ(t_engage.target, "Active");
    EXPECT_EQ(t_engage.event, "EvEngage");
    ASSERT_TRUE(t_engage.guard.has_value());
    ASSERT_TRUE(t_engage.action.has_value());
}

/**
 * @brief Test Intent: Verify Stateflow temporal logic syntax after(N, sec) and after(N, msec).
 *
 * Scenario:
 * - Parse transition with labelString="after(500, msec)".
 * - Parse transition with labelString="after(2, sec)".
 * - Verify synthesized TimeTrigger structures.
 */
TEST(StateflowParserTest, TemporalLogicTriggers) {
    const std::string stateflow_xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Stateflow>\n"
        "    <chart name=\"TimedHeartbeat\">\n"
        "        <state id=\"1\" name=\"PulseHigh\"/>\n"
        "        <state id=\"2\" name=\"PulseLow\"/>\n"
        "        <transition src=\"PulseHigh\" dst=\"PulseLow\" labelString=\"after(500, msec)\"/>\n"
        "        <transition src=\"PulseLow\" dst=\"PulseHigh\" labelString=\"after(2, sec)\"/>\n"
        "    </chart>\n"
        "</Stateflow>\n";

    StateflowParser parser;
    FsmIr model;
    std::string err;
    bool ok = parser.parse(stateflow_xml, model, err);

    ASSERT_TRUE(ok) << "Stateflow parse error: " << err;
    ASSERT_EQ(model.transitions.size(), 2u);

    // Verify after(500, msec)
    const auto& t1 = model.transitions[0];
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(t1.trigger));
    const auto& tt1 = std::get<TimeTrigger>(t1.trigger);
    EXPECT_EQ(tt1.kind, TimeTriggerKind::After);
    EXPECT_EQ(tt1.duration_ms, 500u);

    // Verify after(2, sec) -> 2000 ms
    const auto& t2 = model.transitions[1];
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(t2.trigger));
    const auto& tt2 = std::get<TimeTrigger>(t2.trigger);
    EXPECT_EQ(tt2.kind, TimeTriggerKind::After);
    EXPECT_EQ(tt2.duration_ms, 2000u);
}

/**
 * @brief Test Intent: Verify ParserFactory auto-detection and registration for Stateflow models.
 *
 * Scenario:
 * - Query ParserFactory by format name "stateflow".
 * - Query ParserFactory by extension ".sfx" and ".stateflow".
 * - Query ParserFactory detect_format_from_content on XML containing <Stateflow>.
 */
TEST(StateflowParserTest, ParserFactoryIntegration) {
    auto p1 = ParserFactory::create_by_format("stateflow");
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(p1->format_name(), "stateflow");
    EXPECT_EQ(p1->kind(), FrontendKind::Formal);

    auto p2 = ParserFactory::create_by_extension("controller.sfx");
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(p2->format_name(), "stateflow");

    std::string detected = ParserFactory::detect_format_from_content("<Stateflow><chart name=\"Test\"/></Stateflow>");
    EXPECT_EQ(detected, "stateflow");
}

}  // namespace

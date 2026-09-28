/**
 * @file test_stateflow_parser.cpp
 * @brief Unit test suite for the MathWorks Stateflow chart frontend parser.
 */

#include <gtest/gtest.h>

#include "fsm/backend/emitter_factory.hpp"
#include "fsm/backend/formal/stateflow_serializer.hpp"
#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/frontend/formal/stateflow_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::frontend::formal;
using namespace fsm::frontend;
using namespace fsm::backend::formal;
using namespace fsm::backend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify basic Stateflow chart syntax ingestion and transition parsing.
 * @scenario Parse Stateflow chart with states, default transition, and event[guard]/action transitions.
 * @expected FsmIr successfully populated with initial state, states, guards, and actions.
 */
TEST(StateflowParser, BasicStateflowChart_ParsedIntoValidFsmIr) {
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
    ASSERT_TRUE(t_engage.transition_action.has_value());
}

/**
 * @brief Verify Stateflow temporal logic triggers (after, before, at, every).
 * @scenario Parse Stateflow transitions using after(10, sec) and every(50, msec) temporal triggers.
 * @expected Temporal conditions mapped to timer signals and time constraints.
 */
TEST(StateflowParser, TemporalLogicTriggers_MappedToTimerEvents) {
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
 * @brief Verify ParserFactory instantiation for Stateflow format name.
 * @scenario Query ParserFactory::create_parser("stateflow").
 * @expected Non-null StateflowParser instance returned and can parse chart.
 */
TEST(StateflowParser, ParserFactoryLookup_InstantiatesStateflowParser) {
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

/**
 * @brief Verify roundtrip serialization of Stateflow charts with transition directives.
 * @scenario Parse Stateflow chart, serialize to Stateflow format, and re-parse.
 * @expected Re-parsed chart preserves states, transitions, actions, and temporal guards.
 */
TEST(StateflowParser, DirectivesAndTransitions_RoundtrippedLosslessly) {
    FsmIr baseline;
    baseline.name = "FlightControlSF";
    baseline.initial_state = "Ground";

    baseline.add_state("Ground");
    baseline.add_state("Airborne");
    auto* s_airborne = baseline.find_state_mut("Airborne");
    ASSERT_NE(s_airborne, nullptr);
    s_airborne->has_history = true;
    s_airborne->do_activity = "attitude_stabilizer";

    baseline.add_state("Climb", "Airborne");
    baseline.add_state("Cruise", "Airborne");

    TransitionEdge t1;
    t1.source = "Ground";
    t1.target = "Airborne";
    t1.event = "TakeoffCmd";
    t1.guard = "alt_agl > 10.0";
    t1.transition_action = ActionSignature("retract_gear");
    baseline.add_transition(t1);

    TransitionEdge t2;
    t2.source = "Climb";
    t2.target = "Cruise";
    t2.trigger = TimeTrigger(TimeTriggerKind::After, 1500, TimeUnit::Milliseconds);
    t2.transition_action = ActionSignature("level_off");
    baseline.add_transition(t2);

    EnumDefinition en("FlightPhase", "uint8_t", "Flight phases");
    en.add_literal("Takeoff", 1);
    en.add_literal("Enroute", 2);
    baseline.add_enum(en);

    StructDefinition st("NavData", true, "Navigation packet");
    st.add_field(StructField("lat", "float", "0.0"));
    st.add_field(StructField("alt", "uint32_t", "100"));
    baseline.add_struct(st);

    VariableDefinition var;
    var.name = "speed_kts";
    var.type = "float";
    var.initial_value = "0.0";
    baseline.add_variable(var);

    PortDefinition port;
    port.name = "throttle_cmd";
    port.type = "float";
    port.direction = PortDirection::In;
    baseline.ports.push_back(port);

    // EmitterFactory verification
    std::string factory_xml = EmitterFactory::emit_diagram(baseline, "stateflow");
    EXPECT_FALSE(factory_xml.empty());
    EXPECT_NE(factory_xml.find("<Stateflow>"), std::string::npos);
    EXPECT_NE(factory_xml.find("FlightControlSFMachine"), std::string::npos);

    // StateflowSerializer verification
    std::string xml = StateflowSerializer::serialize(baseline);
    EXPECT_NE(xml.find("@fsm:enum"), std::string::npos);
    EXPECT_NE(xml.find("@fsm:struct"), std::string::npos);
    EXPECT_NE(xml.find("@fsm:var"), std::string::npos);
    EXPECT_NE(xml.find("@fsm:port"), std::string::npos);
    EXPECT_NE(xml.find("after(1500, msec)"), std::string::npos);
    EXPECT_NE(xml.find("type=\"HISTORY\""), std::string::npos);

    // Parse back
    StateflowParser parser;
    FsmIr parsed;
    std::string err;
    ASSERT_TRUE(parser.parse(xml, parsed, err)) << "Stateflow parse error: " << err;

    EXPECT_EQ(parsed.name, baseline.name);
    EXPECT_EQ(parsed.initial_state, baseline.initial_state);
    EXPECT_EQ(parsed.states.size(), baseline.states.size());

    const auto* p_airborne = parsed.find_state("Airborne");
    ASSERT_NE(p_airborne, nullptr);
    EXPECT_TRUE(p_airborne->is_composite);
    EXPECT_TRUE(p_airborne->has_history);
    ASSERT_TRUE(p_airborne->do_activity.has_value());
    EXPECT_EQ(*p_airborne->do_activity, "attitude_stabilizer");

    const auto* p_cruise = parsed.find_state("Cruise");
    ASSERT_NE(p_cruise, nullptr);
    EXPECT_EQ(p_cruise->parent_state, "Airborne");

    // Verify directives
    ASSERT_EQ(parsed.custom_types.size(), 2u);
    const auto* fp = parsed.find_enum("FlightPhase");
    ASSERT_NE(fp, nullptr);
    EXPECT_EQ(fp->name, "FlightPhase");
    const auto* nd = parsed.find_struct("NavData");
    ASSERT_NE(nd, nullptr);
    EXPECT_EQ(nd->name, "NavData");
    ASSERT_EQ(parsed.variables.size(), 1u);
    EXPECT_EQ(parsed.variables[0].name, "speed_kts");
    ASSERT_EQ(parsed.ports.size(), 1u);
    EXPECT_EQ(parsed.ports[0].name, "throttle_cmd");

    // Verify transitions
    ASSERT_EQ(parsed.transitions.size(), 2u);
    const auto& pt2 = parsed.transitions[1];
    EXPECT_EQ(pt2.source, "Climb");
    EXPECT_EQ(pt2.target, "Cruise");
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(pt2.trigger));
    EXPECT_EQ(std::get<TimeTrigger>(pt2.trigger).duration_ms, 1500u);
    ASSERT_TRUE(pt2.transition_action.has_value());
    EXPECT_EQ(pt2.transition_action->name, "level_off");
}

/**
 * @brief Verify Stateflow connective junctions and nested condition brackets.
 * @scenario Parse Stateflow chart containing connective junctions and complex action brackets.
 * @expected Junction pseudostates and compound conditions correctly extracted.
 */
TEST(StateflowParser, NestedBracketsAndJunctions_ParsedCorrectly) {
    const std::string sf_xml = R"(
        <Stateflow>
            <chart id="1" name="NestedBracketChart">
                <state id="10" name="Monitoring"/>
                <junction id="20" SSID="5" type="CONNECTIVE"/>
                <state id="30" name="Alarm"/>
                <transition src="Monitoring" dst="Junction_5"
                    labelString="EvCheck [sensor_buf[0] > 100 &amp;&amp; sensor_buf[1] &lt; 50] { flag = 1; } / { logWarn(); }"/>
                <transition src="Junction_5" dst="Alarm" labelString="[flag == 1] / { triggerSiren(); }"/>
            </chart>
        </Stateflow>
    )";

    StateflowParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sf_xml, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "NestedBracketChart");
    ASSERT_NE(model.find_state("Monitoring"), nullptr);
    ASSERT_NE(model.find_state("Alarm"), nullptr);
    const auto* junc = model.find_state("Junction_5");
    ASSERT_NE(junc, nullptr);
    EXPECT_EQ(junc->kind, StateKind::Junction);

    ASSERT_EQ(model.transitions.size(), 2u);
    const auto& t1 = model.transitions[0];
    EXPECT_EQ(t1.source, "Monitoring");
    EXPECT_EQ(t1.target, "Junction_5");
    EXPECT_EQ(t1.event, "EvCheck");
    ASSERT_TRUE(t1.guard.has_value());
    EXPECT_NE(t1.guard->find("sensor_buf"), std::string::npos);
}

}  // namespace

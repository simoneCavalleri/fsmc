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

/**
 * @brief Test Intent: Verify StateflowSerializer XML generation, EmitterFactory registration, and full lossless parsing.
 *
 * Scenario:
 * - Construct FsmIr with hierarchy, parallel state, history junction, do_activity, directives (var, port, enum, struct).
 * - Serialize to Stateflow XML using StateflowSerializer and EmitterFactory.
 * - Parse back with StateflowParser and verify all structural and metadata elements.
 */
TEST(StateflowParserTest, SerializerRoundtripWithDirectives) {
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
    t1.action = "retract_gear";
    baseline.add_transition(t1);

    TransitionEdge t2;
    t2.source = "Climb";
    t2.target = "Cruise";
    t2.trigger = TimeTrigger(TimeTriggerKind::After, 1500, TimeUnit::Milliseconds);
    t2.action = "level_off";
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
    ASSERT_TRUE(pt2.action.has_value());
    EXPECT_EQ(*pt2.action, "level_off");
}

}  // namespace


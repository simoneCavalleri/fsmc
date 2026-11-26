/**
 * @file test_stateflow_parser.cpp
 * @brief Unit test suite for the MathWorks Stateflow chart frontend parser.
 */

#include <gtest/gtest.h>

#include "fsm/backend/emitter_factory.hpp"
#include "fsm/backend/formal/stateflow_serializer.hpp"
#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/frontend/formal/stateflow/stateflow_parser.hpp"
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
    fsm::diagnostic::DiagnosticEngine err;
    bool ok = parser.parse(stateflow_xml, model, err);

    ASSERT_TRUE(ok) << "Stateflow parse error: " << err.render_to_string();
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
    fsm::diagnostic::DiagnosticEngine err;
    bool ok = parser.parse(stateflow_xml, model, err);

    ASSERT_TRUE(ok) << "Stateflow parse error: " << err.render_to_string();
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
    EXPECT_NE(xml.find("<P Name=\"type\">HISTORY_JUNCTION</P>"), std::string::npos);
    EXPECT_NE(xml.find("<Children>"), std::string::npos);

    // Parse back
    StateflowParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, parsed, err)) << "Stateflow parse error: " << err.render_to_string();

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
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sf_xml, model, err)) << "Error: " << err.render_to_string();

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

/**
 * @brief Verify Stateflow state actions (en, du, ex) and periodic/at temporal logic.
 * @scenario Parse Stateflow chart containing state actions in labelString, attributes, and every() trigger.
 * @expected Entry/exit actions and do_activity extracted into StateNode, every() parsed as TimeTrigger,
 *           and stateflow serialization preserves entry, during, exit, and every() syntax.
 */
TEST(StateflowParser, StateActionsAndPeriodicTimers_ParsedAndSerialized) {
    const std::string sf_xml = R"(
        <Stateflow>
            <chart id="1" name="TemporalActionsChart">
                <state id="10" name="Active" labelString="Active&#10;en: initHardware();&#10;du: monitorSensors();&#10;ex: cleanupHardware();"/>
                <state id="20" name="Standby" entry="enterSleep();" exit="wakeUp();"/>
                <transition src="Active" dst="Standby" labelString="every(50, ms) [batteryLow] / { sendTelemetry(); }"/>
            </chart>
        </Stateflow>
    )";

    StateflowParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sf_xml, model, err)) << "Error: " << err.render_to_string();

    // Verify Active state actions from labelString
    const auto* active_st = model.find_state("Active");
    ASSERT_NE(active_st, nullptr);
    ASSERT_FALSE(active_st->entry_actions.empty());
    EXPECT_EQ(active_st->entry_actions[0].name, "initHardware");
    ASSERT_TRUE(active_st->do_activity.has_value());
    EXPECT_EQ(*active_st->do_activity, "monitorSensors");
    ASSERT_FALSE(active_st->exit_actions.empty());
    EXPECT_EQ(active_st->exit_actions[0].name, "cleanupHardware");

    // Verify Standby state actions from attributes
    const auto* standby_st = model.find_state("Standby");
    ASSERT_NE(standby_st, nullptr);
    ASSERT_FALSE(standby_st->entry_actions.empty());
    EXPECT_EQ(standby_st->entry_actions[0].name, "enterSleep");
    ASSERT_FALSE(standby_st->exit_actions.empty());
    EXPECT_EQ(standby_st->exit_actions[0].name, "wakeUp");

    // Verify transition with every() trigger
    ASSERT_EQ(model.transitions.size(), 1u);
    const auto& tr = model.transitions[0];
    EXPECT_EQ(tr.source, "Active");
    EXPECT_EQ(tr.target, "Standby");
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(tr.trigger));
    const auto& tt = std::get<TimeTrigger>(tr.trigger);
    EXPECT_EQ(tt.kind, TimeTriggerKind::Every);
    EXPECT_EQ(tt.duration_ms, 50u);

    // Roundtrip serialize to standard MathWorks simulink/stateflow.xml
    std::string exported_xml = StateflowSerializer::serialize(model);

    EXPECT_NE(exported_xml.find("entry: initHardware"), std::string::npos);
    EXPECT_NE(exported_xml.find("during: monitorSensors"), std::string::npos);
    EXPECT_NE(exported_xml.find("exit: cleanupHardware"), std::string::npos);
    EXPECT_NE(exported_xml.find("entry: enterSleep"), std::string::npos);
    EXPECT_NE(exported_xml.find("exit: wakeUp"), std::string::npos);
    EXPECT_NE(exported_xml.find("every(50, msec)"), std::string::npos);
}

/**
 * @brief Verify Stateflow fractional time duration parsing and guard expression preservation.
 * @scenario Parse after(1.5, sec) and complex comparison guard in Stateflow XML.
 * @expected Duration converted to 1500 ms (not truncated to 1 ms) and guard raw expression preserved in model.guards.
 */
TEST(StateflowParser, FractionalDurationsAndGuardRawExpressions_Preserved) {
    const std::string sf_xml = R"(
        <Stateflow>
            <chart id="1" name="FractionalTimerChart">
                <state id="10" name="Preheat"/>
                <state id="20" name="Cook"/>
                <transition src="Preheat" dst="Cook" labelString="after(1.5, sec) [temp &gt;= 180.5] / { startBaking(); }"/>
                <transition src="Cook" dst="Preheat" labelString="every(2.5, s) [timer_expired == 1]"/>
            </chart>
        </Stateflow>
    )";

    StateflowParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sf_xml, model, err)) << "Error: " << err.render_to_string();

    ASSERT_EQ(model.transitions.size(), 2u);

    // Verify 1.5 sec -> 1500 ms
    const auto& t1 = model.transitions[0];
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(t1.trigger));
    const auto& tt1 = std::get<TimeTrigger>(t1.trigger);
    EXPECT_EQ(tt1.kind, TimeTriggerKind::After);
    EXPECT_EQ(tt1.duration_ms, 1500u);

    // Verify 2.5 s -> 2500 ms
    const auto& t2 = model.transitions[1];
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(t2.trigger));
    const auto& tt2 = std::get<TimeTrigger>(t2.trigger);
    EXPECT_EQ(tt2.kind, TimeTriggerKind::Every);
    EXPECT_EQ(tt2.duration_ms, 2500u);

    // Verify guard preservation in model.guards
    ASSERT_FALSE(model.guards.empty());
    auto it = std::find_if(model.guards.begin(), model.guards.end(), [](const GuardModel& gm) {
        return gm.raw_expression.has_value() && gm.raw_expression->find("temp >= 180.5") != std::string::npos;
    });
    EXPECT_NE(it, model.guards.end());

    // Verify StateflowSerializer roundtrip preserves the predicate
    std::string xml = StateflowSerializer::serialize(model);
    EXPECT_NE(xml.find("after(1500, msec)"), std::string::npos);
    EXPECT_NE(xml.find("every(2500, msec)"), std::string::npos);
    EXPECT_NE(xml.find("temp &gt;= 180.5"), std::string::npos);
}

/**
 * @brief Regression: composite state initial_substate is preserved by StateflowParser and StateflowSerializer.
 * @scenario Parse a Stateflow chart with a composite state using `<transition dst="..."/>` and `initial="..."`,
 *           then serialize and re-parse it.
 * @expected Both forms populate `initial_substate` on the composite StateNode and survive roundtrip serialization.
 */
TEST(StateflowParser, CompositeInitialSubstate_RoundtripsViaAttributeAndInitialTransition) {
    const std::string sf_xml = R"(
        <Stateflow>
            <chart id="1" name="CompositeInitChart" initial="Off">
                <state id="10" name="Off"/>
                <state id="20" name="Active">
                    <transition id="100" dst="Idle"/>
                    <state id="21" name="Idle"/>
                    <state id="22" name="Running"/>
                </state>
            </chart>
        </Stateflow>
    )";

    StateflowParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sf_xml, model, err)) << "Error: " << err.render_to_string();

    const auto* active = model.find_state("Active");
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->initial_sub_state, "Idle");

    const std::string serialized = StateflowSerializer::serialize(model);
    EXPECT_EQ(serialized.find("<P Name=\"initial\">"), std::string::npos) << serialized;
    EXPECT_NE(serialized.find("<dst><P Name=\"SSID\">"), std::string::npos) << serialized;

    FsmIr reparsed;
    ASSERT_TRUE(parser.parse(serialized, reparsed, err)) << "Re-parse error: " << err.render_to_string();
    const auto* reparsed_active = reparsed.find_state("Active");
    ASSERT_NE(reparsed_active, nullptr);
    EXPECT_EQ(reparsed_active->initial_sub_state, "Idle");
}

/**
 * @brief Regression: Stateflow transition action blocks with variable assignments populate
 * `ActionSignature::assignments`.
 * @scenario Parse a Stateflow transition with `labelString="EvGo [a &gt; 0 &amp;&amp; b &lt; 10] / { x = a; y += 1 }"`.
 * @expected `transition_action->assignments` contains `x = a` and `y = y + 1` rather than sanitizing `x = a` as a
 *           service call name.
 */
TEST(StateflowParser, TransitionActionAssignments_PopulatedInActionSignature) {
    const std::string sf_xml = R"(
        <Stateflow>
            <chart id="1" name="SfAssignChart" initial="Idle">
                <state id="10" name="Idle"/>
                <state id="20" name="Done"/>
                <transition src="Idle" dst="Done"
                    labelString="EvGo [a &gt; 0 &amp;&amp; b &lt; 10] / { x = a; y += 1 }"/>
            </chart>
        </Stateflow>
    )";

    StateflowParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sf_xml, model, err)) << "Error: " << err.render_to_string();

    ASSERT_EQ(model.transitions.size(), 1u);
    const auto& tr = model.transitions[0];
    ASSERT_TRUE(tr.transition_action.has_value());
    ASSERT_EQ(tr.transition_action->assignments.size(), 2u);
    EXPECT_EQ(tr.transition_action->assignments[0].target.name, "x");
    EXPECT_EQ(tr.transition_action->assignments[0].expression, "a");
    EXPECT_EQ(tr.transition_action->assignments[1].target.name, "y");
    EXPECT_EQ(tr.transition_action->assignments[1].expression, "y + 1");
}

/**
 * @brief Regression: Stateflow state `entry` / `exit` attributes with variable assignments populate `assignments`.
 * @scenario Parse a Stateflow chart with `entry="x = 10" exit="x = 20"` on `S1` and `entry="x += 5"` on `S2`.
 * @expected Both `entry_actions` and `exit_actions` contain structured `ActionAssignment` entries.
 */
TEST(StateflowParser, StateLifecycleAssignments_PopulatedInEntryAndExitActions) {
    const std::string sf_xml = R"(
        <Stateflow>
            <chart id="1" name="SfStateAssign" initial="S1">
                <state id="10" name="S1" entry="x = 10" exit="x = 20"/>
                <state id="20" name="S2" entry="x += 5"/>
                <transition src="S1" dst="S2" labelString="EvNext"/>
            </chart>
        </Stateflow>
    )";

    StateflowParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sf_xml, model, err)) << "Error: " << err.render_to_string();

    const auto* s1 = model.find_state("S1");
    ASSERT_NE(s1, nullptr);
    ASSERT_EQ(s1->entry_actions.size(), 1u);
    ASSERT_EQ(s1->entry_actions[0].assignments.size(), 1u);
    EXPECT_EQ(s1->entry_actions[0].assignments[0].target.name, "x");
    EXPECT_EQ(s1->entry_actions[0].assignments[0].expression, "10");

    ASSERT_EQ(s1->exit_actions.size(), 1u);
    ASSERT_EQ(s1->exit_actions[0].assignments.size(), 1u);
    EXPECT_EQ(s1->exit_actions[0].assignments[0].target.name, "x");
    EXPECT_EQ(s1->exit_actions[0].assignments[0].expression, "20");

    const auto* s2 = model.find_state("S2");
    ASSERT_NE(s2, nullptr);
    ASSERT_EQ(s2->entry_actions.size(), 1u);
    ASSERT_EQ(s2->entry_actions[0].assignments.size(), 1u);
    EXPECT_EQ(s2->entry_actions[0].assignments[0].target.name, "x");
    EXPECT_EQ(s2->entry_actions[0].assignments[0].expression, "x + 5");
}

/**
 * @brief Verify StateflowSerializer and StateflowParser preserve assignments and normalized guards on roundtrip.
 * @scenario Serialize an FsmIr with entry/exit/transition assignments and a guard, then re-parse the XML.
 * @expected All assignment expressions and guard normalized_expression values survive the roundtrip.
 */
TEST(StateflowParser, RoundtripAssignmentsAndNormalizedGuards_PreservedInXml) {
    FsmIr orig;
    orig.name = "SfRt";
    orig.initial_state = "Ready";
    orig.add_variable(VariableDefinition("x", DataType{PrimitiveTypeKind::Int32}, "0"));

    StateNode ready("Ready");
    ActionSignature entry_act("Ready_entry_x");
    entry_act.assignments.emplace_back(LValueTarget("x"), "10");
    ready.entry_actions.push_back(std::move(entry_act));

    ActionSignature exit_act("Ready_exit_x");
    exit_act.assignments.emplace_back(LValueTarget("x"), "x + 1");
    ready.exit_actions.push_back(std::move(exit_act));

    StateNode running("Running");
    orig.states.push_back(std::move(ready));
    orig.states.push_back(std::move(running));

    orig.add_guard("x_gte_10", "", std::optional<std::string>{"x >= 10"}, std::optional<std::string>{"x >= 10"});
    ActionSignature tr_act("tr_x");
    tr_act.assignments.emplace_back(LValueTarget("x"), "x * 2");
    orig.transitions.emplace_back("Ready", "Running", "START", "x_gte_10", std::move(tr_act));

    const std::string xml = StateflowSerializer::serialize(orig);
    StateflowParser parser;
    FsmIr rt;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, rt, err)) << "Error: " << err.render_to_string();

    const auto* rt_ready = rt.find_state("Ready");
    ASSERT_NE(rt_ready, nullptr);
    ASSERT_EQ(rt_ready->entry_actions.size(), 1u);
    ASSERT_EQ(rt_ready->entry_actions[0].assignments.size(), 1u);
    EXPECT_EQ(rt_ready->entry_actions[0].assignments[0].expression, "10");

    ASSERT_EQ(rt_ready->exit_actions.size(), 1u);
    ASSERT_EQ(rt_ready->exit_actions[0].assignments.size(), 1u);
    EXPECT_EQ(rt_ready->exit_actions[0].assignments[0].expression, "x + 1");

    ASSERT_EQ(rt.transitions.size(), 1u);
    ASSERT_TRUE(rt.transitions[0].transition_action.has_value());
    ASSERT_EQ(rt.transitions[0].transition_action->assignments.size(), 1u);
    EXPECT_EQ(rt.transitions[0].transition_action->assignments[0].expression, "x * 2");

    ASSERT_FALSE(rt.guards.empty());
    ASSERT_TRUE(rt.guards[0].normalized_expression.has_value());
    EXPECT_EQ(*rt.guards[0].normalized_expression, "x >= 10");
}

/**
 * @brief Verify StateflowParser populates orthogonal_regions for decomposition="PARALLEL_AND" states.
 * @scenario Stateflow XML with parallel state 'Operational' containing two child region states Reg1 and Reg2.
 * @expected 'Operational' has StateKind::Parallel, empty initial_sub_state, and 2 orthogonal_regions.
 */
TEST(StateflowParserTest, ParsesParallelAndDecompositionIntoOrthogonalRegions) {
    const std::string xml = R"xml(
<ModelInformation>
  <Stateflow>
    <machine name="ParSf">
      <chart name="ParSf" initial="Operational">
        <state id="1" name="Operational" decomposition="PARALLEL_AND">
          <state id="2" name="Reg1" initial="R1A">
            <state id="3" name="R1A"/>
            <state id="4" name="R1B"/>
          </state>
          <state id="5" name="Reg2" initial="R2A">
            <state id="6" name="R2A"/>
            <state id="7" name="R2B"/>
          </state>
        </state>
      </chart>
    </machine>
  </Stateflow>
</ModelInformation>
)xml";

    StateflowParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, ir, err)) << "Error: " << err.render_to_string();

    const auto* op = ir.find_state("Operational");
    ASSERT_NE(op, nullptr);
    EXPECT_EQ(op->kind, StateKind::Parallel);
    EXPECT_TRUE(op->is_composite);
    EXPECT_TRUE(op->initial_sub_state.empty());
    ASSERT_EQ(op->orthogonal_regions.size(), 2u);
    EXPECT_EQ(op->orthogonal_regions[0].name, "Reg1");
    EXPECT_EQ(op->orthogonal_regions[0].initial_state_id, "R1A");
    EXPECT_EQ(op->orthogonal_regions[1].name, "Reg2");
    EXPECT_EQ(op->orthogonal_regions[1].initial_state_id, "R2A");
}

/**
 * @brief Verify StateflowParser marks external transitions entering a composite state with a HISTORY junction as
 *        targeting history.
 * @scenario Stateflow XML with composite 'Work' containing `<junction type="HISTORY"/>` and external `Paused -> Work`.
 * @expected Transition `Paused -> Work` has `target_is_history == true` (and `target_is_deep_history == false`).
 */
TEST(StateflowParserTest, MarksIncomingTransitionsToHistoryCompositeAsHistoryTargets) {
    const std::string xml = R"xml(
<ModelInformation>
  <Stateflow>
    <machine name="SfHist">
      <chart name="SfHist" initial="Paused">
        <state id="1" name="Paused"/>
        <state id="2" name="Work" initial="S1">
          <junction id="10" type="HISTORY"/>
          <state id="3" name="S1"/>
          <state id="4" name="S2"/>
        </state>
        <transition src="Paused" dst="Work" labelString="RESUME"/>
      </chart>
    </machine>
  </Stateflow>
</ModelInformation>
)xml";

    StateflowParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, ir, err)) << "Error: " << err.render_to_string();

    ASSERT_EQ(ir.transitions.size(), 1u);
    EXPECT_TRUE(ir.transitions[0].target_is_history);
    EXPECT_FALSE(ir.transitions[0].target_is_deep_history);
}

/**
 * @brief Regression: Stateflow MATLAB `~=` and `~` guard operators are normalized to `!=` and `!`.
 * @scenario Parse a Stateflow transition with `labelString="Go[x ~= 0 && ~fault]"`.
 * @expected Guard atoms contain `"x != 0"` and `"fault"` negated via `fsm::not_<fault>`.
 */
TEST(StateflowParser, MatlabNegationOperators_NormalizedToCpp) {
    const std::string xml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<Stateflow>
  <machine name="SfMatlab">
    <chart id="1" name="SfMatlabChart" initial="Idle">
      <transition id="100" dst="Idle"/>
      <state id="10" name="Idle"/>
      <state id="11" name="Active"/>
      <transition id="101" src="Idle" dst="Active" labelString="Go[x ~= 0 &amp;&amp; ~fault]"/>
    </chart>
  </machine>
</Stateflow>
)xml";

    StateflowParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, ir, err)) << "Error: " << err.render_to_string();
    ASSERT_EQ(ir.transitions.size(), 1u);
    ASSERT_TRUE(ir.transitions[0].guard.has_value());
    EXPECT_NE(ir.transitions[0].guard->find("!fault"), std::string::npos);
    bool found_neq = false;
    for (const auto& g : ir.guards) {
        if (g.raw_expression.has_value() && *g.raw_expression == "x != 0") {
            found_neq = true;
        }
    }
    EXPECT_TRUE(found_neq);
}

/**
 * @brief Regression: `StateflowParser` strips MATLAB `%` line comments inside guard and action
 * expressions so comments like `[x == 0 % check zero]` do not corrupt the guard expression.
 * @scenario Parse Stateflow transition `Go [x == 0 % check zero] / x = 1; % done`.
 * @expected Guard `raw_expression == "x == 0"` and action assigns `x = 1`.
 */
TEST(StateflowParser, MatlabPercentCommentsInGuardAndAction_StrippedCleanly) {
    const std::string xml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<Stateflow>
  <machine name="SfGComment">
    <chart id="1" name="SfGComment">
      <!-- @fsm:var name=x type=int init=0 -->
      <state id="10" name="Idle"/>
      <state id="20" name="Done"/>
      <transition src="Idle" dst="Done" labelString="Go [x == 0 % check zero] / x = 1; % done"/>
    </chart>
  </machine>
</Stateflow>
)xml";

    StateflowParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, ir, err)) << "Error: " << err.render_to_string();
    ASSERT_EQ(ir.guards.size(), 1u);
    EXPECT_EQ(ir.guards[0].raw_expression, "x == 0");
    ASSERT_EQ(ir.transitions.size(), 1u);
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    ASSERT_EQ(ir.transitions[0].transition_action->assignments.size(), 1u);
    EXPECT_EQ(ir.transitions[0].transition_action->assignments[0].target, "x");
    EXPECT_EQ(ir.transitions[0].transition_action->assignments[0].expression, "1");
}

/**
 * @brief Regression: Multiple Stateflow transitions between the same `source` and `target` states
 * assigning to the same variable receive disambiguated synthetic action names (`Idle_Done_act_x`,
 * `Idle_Done_act_x_2`) so their assignments do not collide in C++ codegen.
 * @scenario Parse Stateflow XML with `Ev1 / x = 1` and `Ev2 / x = 2` from `Idle` to `Done`.
 * @expected `transitions[0].transition_action->name != transitions[1].transition_action->name`.
 */
TEST(StateflowParser, MultipleTransitionsSameEndpoints_DisambiguateAssignmentActionNames) {
    const std::string xml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<Stateflow>
  <machine name="SfSameEp">
    <chart id="1" name="SfSameEp">
      <!-- @fsm:var name=x type=int init=0 -->
      <state id="10" name="Idle"/>
      <state id="20" name="Done"/>
      <transition src="Idle" dst="Done" labelString="Ev1 / x = 1;"/>
      <transition src="Idle" dst="Done" labelString="Ev2 / x = 2;"/>
    </chart>
  </machine>
</Stateflow>
)xml";

    StateflowParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, ir, err)) << "Error: " << err.render_to_string();
    ASSERT_EQ(ir.transitions.size(), 2u);
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    ASSERT_TRUE(ir.transitions[1].transition_action.has_value());
    EXPECT_NE(ir.transitions[0].transition_action->name, ir.transitions[1].transition_action->name);
}

/**
 * @brief Regression: `StateflowSerializer` preserves `model.properties`, `state.traceability_reqs`,
 * and `state.time_invariant` across Stateflow XML roundtrips.
 * @scenario Serialize an FSM with `FormalProperty("safe", "G (not Bad)")` and state `Active` (`REQ_001`, `500[ms]`),
 * then re-parse with `StateflowParser`.
 * @expected Re-parsed IR keeps the property, `traceability_reqs == {"REQ_001"}`, and `time_invariant == "500[ms]"`.
 */
TEST(StateflowParser, PropertiesReqsAndTimeInvariant_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "SfMetaRt";
    ir.initial_state = "Active";
    auto& active = ir.add_state("Active");
    active.traceability_reqs.push_back("REQ_001");
    active.time_invariant = "500[ms]";

    FormalProperty prop;
    prop.name = "safe";
    prop.kind = PropertyKind::Safety;
    prop.raw_formula = "G (not Bad)";
    ir.add_property(prop);

    const std::string xml = StateflowSerializer::serialize(ir);
    StateflowParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, parsed, err)) << err.render_to_string();

    ASSERT_EQ(parsed.properties.size(), 1u);
    EXPECT_EQ(parsed.properties[0].name, "safe");
    EXPECT_EQ(parsed.properties[0].raw_formula, "G (not Bad)");

    const auto* p_active = parsed.find_state("Active");
    ASSERT_NE(p_active, nullptr);
    ASSERT_EQ(p_active->traceability_reqs.size(), 1u);
    EXPECT_EQ(p_active->traceability_reqs[0], "REQ_001");
    ASSERT_TRUE(p_active->time_invariant.has_value());
    EXPECT_EQ(*p_active->time_invariant, "500[ms]");
}

/**
 * @brief Regression: `StateflowSerializer` emits `when(<expr>)` for `ChangeTrigger` transitions
 * so `StateflowParser` preserves `ChangeTrigger` across Stateflow XML roundtrips.
 * @scenario Serialize a transition with `ChangeTrigger("count > 5")` to Stateflow XML and re-parse.
 * @expected Serialized XML contains `when(count &gt; 5)` and re-parsed transition holds `ChangeTrigger`.
 */
TEST(StateflowParser, ChangeTrigger_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "SfChangeRt";
    ir.initial_state = "Wait";
    ir.add_state("Wait");
    ir.add_state("Done");
    ir.add_variable(VariableDefinition("count", DataType::int32(), "0"));
    TransitionEdge t("Wait", "Done", "");
    t.trigger = ChangeTrigger("count > 5", true);
    ir.add_transition(t);

    const std::string xml = StateflowSerializer::serialize(ir);
    EXPECT_NE(xml.find("<P Name=\"labelString\">when(count &gt; 5)</P>"), std::string::npos) << xml;

    StateflowParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, parsed, err)) << err.render_to_string();
    ASSERT_EQ(parsed.transitions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ChangeTrigger>(parsed.transitions[0].trigger));
    EXPECT_EQ(std::get<ChangeTrigger>(parsed.transitions[0].trigger).raw_expression, "count > 5");
}

/**
 * @brief Verify StateflowParser ingests native MathWorks simulink/stateflow.xml without any @fsm comments.
 * @scenario Parse standard <Stateflow><machine><Children><chart><Children> XML with <state SSID>, <P
 * Name="labelString">, SSID-referenced <transition><src>/<dst>, <junction> HISTORY_JUNCTION, and <data>
 * INPUT_DATA/OUTPUT_DATA/LOCAL_DATA.
 * @expected Chart name, initial state, hierarchy, transitions, history flag, ports, and variables are all extracted.
 */
TEST(StateflowParser, ParsesNativeMathWorksStateflowXmlWithoutFsmComments) {
    const std::string native_sf_xml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<Stateflow>
  <machine id="1">
    <P Name="name">BrakeCtrlMachine</P>
    <Children>
      <chart id="2">
        <P Name="name">BrakeController</P>
        <P Name="decomposition">CLUSTER_CHART</P>
        <Children>
          <state SSID="10">
            <P Name="labelString">Idle&#10;entry: pressure_cmd = 0</P>
            <P Name="decomposition">CLUSTER_STATE</P>
          </state>
          <state SSID="11">
            <P Name="labelString">Engaged</P>
            <P Name="decomposition">CLUSTER_STATE</P>
            <Children>
              <junction SSID="12">
                <P Name="type">HISTORY_JUNCTION</P>
              </junction>
              <state SSID="13">
                <P Name="labelString">Holding</P>
                <P Name="decomposition">CLUSTER_STATE</P>
              </state>
              <transition SSID="14">
                <dst><P Name="SSID">13</P></dst>
              </transition>
            </Children>
          </state>
          <data SSID="20" name="pedal_force">
            <P Name="scope">INPUT_DATA</P>
            <P Name="dataType">int32</P>
          </data>
          <data SSID="21" name="pressure_cmd">
            <P Name="scope">OUTPUT_DATA</P>
            <P Name="dataType">uint32</P>
          </data>
          <data SSID="22" name="cycle_cnt">
            <P Name="scope">LOCAL_DATA</P>
            <P Name="dataType">uint32</P>
          </data>
          <transition SSID="30">
            <dst><P Name="SSID">10</P></dst>
          </transition>
          <transition SSID="31">
            <P Name="labelString">EvPress [pedal_force &gt; 10] / { cycle_cnt = cycle_cnt + 1 }</P>
            <src><P Name="SSID">10</P></src>
            <dst><P Name="SSID">11</P></dst>
          </transition>
        </Children>
      </chart>
    </Children>
  </machine>
</Stateflow>
)xml";

    StateflowParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine diag;
    ASSERT_TRUE(parser.parse(native_sf_xml, model, diag)) << diag.render_to_string();

    EXPECT_EQ(model.name, "BrakeController");
    EXPECT_EQ(model.initial_state, "Idle");
    ASSERT_NE(model.find_state("Idle"), nullptr);
    const auto* engaged = model.find_state("Engaged");
    ASSERT_NE(engaged, nullptr);
    EXPECT_TRUE(engaged->has_history);
    EXPECT_EQ(engaged->initial_sub_state, "Holding");

    const auto* p_in = model.find_port("pedal_force");
    ASSERT_NE(p_in, nullptr);
    EXPECT_TRUE(p_in->is_in());
    EXPECT_EQ(p_in->type.to_cpp_type(), "int32_t");

    const auto* p_out = model.find_port("pressure_cmd");
    ASSERT_NE(p_out, nullptr);
    EXPECT_TRUE(p_out->is_out());
    EXPECT_EQ(p_out->type.to_cpp_type(), "uint32_t");

    const auto* var_local = model.find_variable("cycle_cnt");
    ASSERT_NE(var_local, nullptr);
    EXPECT_EQ(var_local->type.to_cpp_type(), "uint32_t");

    ASSERT_EQ(model.transitions.size(), 1u);
    EXPECT_EQ(model.transitions[0].source, "Idle");
    EXPECT_EQ(model.transitions[0].target, "Engaged");
    EXPECT_EQ(model.transitions[0].event, "EvPress");
    EXPECT_TRUE(model.transitions[0].target_is_history);
}

/**
 * @brief Regression (BUG-39): `StateflowSerializer` and `StateflowParser` preserve Deep History
 * (`target_is_history == true`, `target_is_deep_history == true`) across Stateflow XML roundtrips.
 */
TEST(StateflowParser, DeepHistoryTransition_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "SfDeepHistRt";
    ir.initial_state = "Outer";
    auto& outer = ir.add_state("Outer");
    outer.is_composite = true;
    outer.initial_sub_state = "Sub1";
    ir.add_state("Sub1", "Outer");
    ir.add_state("Paused");

    ir.add_transition(TransitionEdge("Outer", "Paused", "Pause"));
    TransitionEdge resume("Paused", "Outer", "Resume");
    resume.target_is_history = true;
    resume.target_is_deep_history = true;
    ir.add_transition(resume);

    const std::string xml = StateflowSerializer::serialize(ir);
    StateflowParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, parsed, err)) << err.render_to_string();

    bool found_resume = false;
    for (const auto& t : parsed.transitions) {
        if (t.event == "Resume") {
            EXPECT_EQ(t.target, "Outer");
            EXPECT_TRUE(t.target_is_history);
            EXPECT_TRUE(t.target_is_deep_history);
            found_resume = true;
        }
    }
    EXPECT_TRUE(found_resume);
}

}  // namespace

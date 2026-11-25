/**
 * @file test_scxml_parser.cpp
 * @brief Unit test suite for the W3C SCXML (State Chart XML) frontend parser.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/formal/scxml_serializer.hpp"
#include "fsm/frontend/formal/scxml/scxml_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::frontend::formal;
using namespace fsm::frontend;
using namespace fsm::backend::formal;
using namespace fsm::backend::cpp;
using namespace fsm::backend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify W3C SCXML document parsing with internal transitions.
 * @scenario Parse basic SCXML document with state declarations, event transitions, and type=internal.
 * @expected FsmIr populated with states, transitions, and transition type classification.
 */
TEST(ScxmlParser, BasicScxmlDocument_ParsedIntoValidFsmIr) {
    const std::string scxml_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Disconnected" name="ScxmlConnectionFSM">
  <state id="Disconnected">
    <transition event="ConnectCmd" cond="HasNetworkGuard" target="Connecting">
      <send event="InitSocketAction"/>
    </transition>
    <transition event="ConnectCmd" cond="NoNetworkGuard" target="Disconnected">
      <send event="LogErrorAction"/>
    </transition>
  </state>
  <state id="Connecting">
    <transition event="HandshakeOkEvent" target="Connected">
      <send event="SetupSessionAction"/>
    </transition>
    <transition event="HandshakeFailedEvent" target="Disconnected">
      <send event="CleanupAction"/>
    </transition>
  </state>
  <state id="Connected">
    <transition event="Ping">
      <send event="ResetWatchdogAction"/>
    </transition>
    <transition event="DisconnectCmd" target="Disconnected">
      <send event="CloseSocketAction"/>
    </transition>
  </state>
</scxml>
)";

    ScxmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(scxml_content, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.name, "ScxmlConnectionFSM");
    EXPECT_EQ(model.initial_state, "Disconnected");
    EXPECT_EQ(model.states.size(), 3u);
    EXPECT_EQ(model.guards.size(), 2u);
    EXPECT_EQ(model.actions.size(), 6u);

    // Verify internal transition for Ping
    bool found_internal = false;
    for (const auto& trans : model.transitions) {
        if (trans.event == "Ping" && trans.kind == TransitionEdgeKind::Internal) {
            found_internal = true;
            break;
        }
    }
    EXPECT_TRUE(found_internal);
}

/**
 * @brief Verify parsing of real-world industrial press SCXML specification.
 * @scenario Parse SCXML industrial press controller with compound states and guard conditions.
 * @expected Hierarchy, safety guards, and action scripts faithfully imported.
 */
TEST(ScxmlParser, IndustrialPressSnippet_ParsedIntoValidFsmIr) {
    const std::string scxml_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Idle" name="GeneratedFSM">
  <state id="Idle">
    <transition event="PowerOnCmd" cond="SafetyOk" target="Initializing" action="LogPowerOn"/>
  </state>
  <state id="Initializing">
    <transition event="InitDone" target="Ready" action="StoreDiagnostics"/>
    <transition event="AbortCmd" target="Idle" action="Cleanup"/>
  </state>
  <state id="Ready">
    <transition event="StartCmd" cond="ToolLoaded" target="Running" action="EngageDrive"/>
  </state>
  <state id="Running">
    <transition event="PauseCmd" target="Paused" action="HoldPosition"/>
    <transition event="EStopEvent" target="Faulted" action="EmergencyBrake"/>
    <transition event="StopCmd" target="Idle" action="DisengageDrive"/>
  </state>
  <state id="Paused">
    <transition event="ResumeCmd" cond="SafetyOk" target="Running" action="ReleaseHold"/>
  </state>
  <state id="Faulted">
    <transition event="ResetFaultCmd" target="Idle" action="ClearFault"/>
  </state>
</scxml>)";

    ScxmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(scxml_content, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.states.size(), 6U);
    EXPECT_EQ(model.transitions.size(), 9U);
    EXPECT_EQ(model.initial_state, "Idle");
    EXPECT_EQ(model.actions.size(), 9U);
    EXPECT_EQ(model.guards.size(), 2U);
}

/**
 * @brief Verify robustness against SCXML attribute permutations and self-closing state tags.
 * @scenario Parse SCXML with varied attribute orders, empty states, and self-closing tags.
 * @expected Parser processes document without syntax errors or missing nodes.
 */
TEST(ScxmlParser, AttributePermutationsAndSelfClosingStates_ParsedCorrectly) {
    const std::string scxml_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="S1">
  <state id="S1">
    <transition target="S2" cond="GuardA" event="Evt1" action="Act1"/>
    <transition action="Act2" event="Evt2" target="S3"/>
  </state>
  <state id="S2">
    <transition target="S3" event="Evt3"/>
  </state>
  <state id="S3"/>
</scxml>)";

    ScxmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(scxml_content, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.states.size(), 3U);
    EXPECT_EQ(model.transitions.size(), 3U);
    EXPECT_EQ(model.actions.size(), 2U);
}

/**
 * @brief Verify SCXML <datamodel>, <onentry>, and <onexit> lifecycle hooks.
 * @scenario Parse SCXML declaring data variables, initial values, and entry/exit scripts.
 * @expected FsmIr variables populated and lifecycle actions captured in state nodes.
 */
TEST(ScxmlParser, NativeDatamodelAndLifecycleHooks_CapturedInIr) {
    const std::string scxml_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Init" name="DatamodelFSM">
  <datamodel>
    <data id="retry_count" expr="0" type="uint32_t"/>
    <data id="battery_mv" expr="3700" type="uint32_t"/>
  </datamodel>

  <state id="Init">
    <onentry>
      <assign location="retry_count" expr="0"/>
      <send event="InitSensors"/>
    </onentry>
    <onexit>
      <send event="Cleanup"/>
    </onexit>
    <transition event="StartCmd" target="Active">
      <assign location="retry_count" expr="retry_count + 1"/>
      <send event="LogStart"/>
    </transition>
  </state>
  <state id="Active"/>
</scxml>)";

    ScxmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(scxml_content, model, err)) << "Error: " << err.render_to_string();

    // Check datamodel variables
    ASSERT_EQ(model.variables.size(), 2u);
    EXPECT_EQ(model.variables[0].name, "retry_count");
    EXPECT_EQ(model.variables[0].initial_value, "0");
    EXPECT_EQ(model.variables[1].name, "battery_mv");

    // Check Init state entry/exit
    const auto* init_st = model.find_state("Init");
    ASSERT_NE(init_st, nullptr);
    ASSERT_EQ(init_st->entry_actions.size(), 1u);
    EXPECT_EQ(init_st->entry_actions[0].name, "InitSensors");
    ASSERT_EQ(init_st->entry_actions[0].assignments.size(), 1u);
    EXPECT_EQ(init_st->entry_actions[0].assignments[0].target, "retry_count");

    ASSERT_EQ(init_st->exit_actions.size(), 1u);
    EXPECT_EQ(init_st->exit_actions[0].name, "Cleanup");

    // Check transition assignments
    ASSERT_EQ(model.transitions.size(), 1u);
    ASSERT_TRUE(model.transitions[0].transition_action.has_value());
    ASSERT_EQ(model.transitions[0].transition_action->assignments.size(), 1u);
    EXPECT_EQ(model.transitions[0].transition_action->assignments[0].target, "retry_count");
}

/**
 * @brief Verify SCXML raw boolean condition preservation and roundtrip serialization.
 * @scenario Parse SCXML transition with condition `cond="voltage > 12.5 &amp;&amp; current &lt; 2.0"`.
 * @expected Condition expression preserved in model.guards and emitted accurately by ScxmlSerializer.
 */
TEST(ScxmlParser, RawBooleanConditions_PreservedInGuardsAndSerialized) {
    const std::string scxml_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Charging">
  <state id="Charging">
    <transition event="Tick" target="Float" cond="voltage > 12.5 &amp;&amp; current &lt; 2.0"/>
  </state>
  <state id="Float"/>
</scxml>)";

    ScxmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(scxml_content, model, err)) << "Error: " << err.render_to_string();

    ASSERT_EQ(model.transitions.size(), 1u);
    ASSERT_TRUE(model.transitions[0].guard.has_value());

    // Check that atomic guard details were registered in model.guards
    bool found_expr = false;
    for (const auto& gm : model.guards) {
        if (gm.raw_expression.has_value() && (gm.raw_expression->find("voltage > 12.5") != std::string::npos ||
                                              gm.raw_expression->find("current < 2.0") != std::string::npos)) {
            found_expr = true;
            break;
        }
    }
    EXPECT_TRUE(found_expr) << "Atomic guard expression not found in model.guards";

    // Roundtrip through ScxmlSerializer
    std::string xml = ScxmlSerializer::serialize(model);
    EXPECT_NE(xml.find("voltage &gt; 12.5"), std::string::npos);
    EXPECT_NE(xml.find("current &lt; 2.0"), std::string::npos);
}

/**
 * @brief Verify SCXML <data> type inference when 'type' attribute is omitted.
 * @scenario Parse <datamodel> with boolean, floating-point, negative, and unsigned expressions.
 * @expected Variables receive inferred types (bool, double, int32_t, uint32_t) instead of always uint32_t.
 */
TEST(ScxmlParserTest, InfersDataModelVariableTypesFromExpr) {
    const std::string scxml_content = R"(
        <scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Idle">
            <datamodel>
                <data id="active" expr="false"/>
                <data id="gain" expr="1.5"/>
                <data id="offset" expr="-10"/>
                <data id="retries" expr="0"/>
            </datamodel>
            <state id="Idle"/>
        </scxml>
    )";

    ScxmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(scxml_content, model, err)) << err.render_to_string();

    const auto* active = model.find_variable("active");
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->type, "bool");

    const auto* gain = model.find_variable("gain");
    ASSERT_NE(gain, nullptr);
    EXPECT_EQ(gain->type, "double");

    const auto* offset = model.find_variable("offset");
    ASSERT_NE(offset, nullptr);
    EXPECT_EQ(offset->type, "int32_t");

    const auto* retries = model.find_variable("retries");
    ASSERT_NE(retries, nullptr);
    EXPECT_EQ(retries->type, "uint32_t");
}

/**
 * @brief Verify SCXML <parallel> registers orthogonal regions without putting region IDs into state_ids.
 * @scenario <parallel id="Par"> with two <state> regions Reg1 (R1A, R1B) and Reg2 (R2A, R2B).
 * @expected Regions have empty state_ids so OrthogonalProductPass discovers their nested substates.
 */
TEST(ScxmlParserTest, ParallelRegionsLeaveStateIdsEmptyForSubstateDiscovery) {
    const std::string scxml_content = R"xml(
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Par">
  <parallel id="Par">
    <state id="Reg1" initial="R1A">
      <state id="R1A"><transition event="go" target="R1B"/></state>
      <state id="R1B"/>
    </state>
    <state id="Reg2" initial="R2A">
      <state id="R2A"><transition event="sync" target="R2B"/></state>
      <state id="R2B"/>
    </state>
  </parallel>
</scxml>
)xml";

    ScxmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(scxml_content, model, err)) << err.render_to_string();

    const StateNode* par = model.find_state("Par");
    ASSERT_NE(par, nullptr);
    EXPECT_EQ(par->kind, StateKind::Parallel);
    ASSERT_EQ(par->orthogonal_regions.size(), 2u);
    EXPECT_TRUE(par->orthogonal_regions[0].state_ids.empty());
    EXPECT_TRUE(par->orthogonal_regions[1].state_ids.empty());
    EXPECT_EQ(par->orthogonal_regions[0].initial_state_id, "R1A");
    EXPECT_EQ(par->orthogonal_regions[1].initial_state_id, "R2A");
}

/**
 * @brief Verify SCXML time-trigger event names (after_50ms, every_2s) populate TransitionEdge::trigger.
 * @scenario Parse SCXML with transitions triggered by `after_50ms` and `every_2s`.
 * @expected Transitions hold `TimeTrigger` variants with matching kind and millisecond durations.
 */
TEST(ScxmlParserTest, ParsesAfterAndEveryTimeTriggerEvents) {
    const std::string scxml_content = R"xml(
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Wait">
  <state id="Wait">
    <transition event="after_50ms" target="Done"/>
    <transition event="every_2s" target="Wait"/>
  </state>
  <state id="Done"/>
</scxml>
)xml";

    ScxmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(scxml_content, model, err)) << err.render_to_string();

    ASSERT_EQ(model.transitions.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(model.transitions[0].trigger));
    const auto& tt0 = std::get<TimeTrigger>(model.transitions[0].trigger);
    EXPECT_EQ(tt0.kind, TimeTriggerKind::After);
    EXPECT_EQ(tt0.duration_in_ms(), 50u);

    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(model.transitions[1].trigger));
    const auto& tt1 = std::get<TimeTrigger>(model.transitions[1].trigger);
    EXPECT_EQ(tt1.kind, TimeTriggerKind::Every);
    EXPECT_EQ(tt1.duration_in_ms(), 2000u);
}

/**
 * @brief Verify ScxmlSerializer emits `<parallel>` for StateKind::Parallel states without `initial` attribute.
 * @scenario Parallel state 'Par' with two orthogonal regions Reg1 (R1A, R1B) and Reg2 (R2A, R2B).
 * @expected Serialized SCXML contains `<parallel id="Par">` and roundtrips via ScxmlParser with 2 orthogonal regions.
 */
TEST(ScxmlSerializerTest, SerializesParallelStateAsParallelElement) {
    FsmIr ir;
    ir.name = "ParRoundtrip";
    ir.initial_state = "Par";

    auto& par = ir.add_state("Par");
    par.is_composite = true;
    par.kind = StateKind::Parallel;
    par.orthogonal_regions.emplace_back("Reg1", "Reg1", "R1A");
    par.orthogonal_regions.emplace_back("Reg2", "Reg2", "R2A");

    auto& reg1 = ir.add_state("Reg1", "Par");
    reg1.is_composite = true;
    reg1.initial_sub_state = "R1A";
    ir.add_state("R1A", "Reg1");
    ir.add_state("R1B", "Reg1");

    auto& reg2 = ir.add_state("Reg2", "Par");
    reg2.is_composite = true;
    reg2.initial_sub_state = "R2A";
    ir.add_state("R2A", "Reg2");
    ir.add_state("R2B", "Reg2");

    ir.add_transition(TransitionEdge("R1A", "R1B", "E1"));
    ir.add_transition(TransitionEdge("R2A", "R2B", "E2"));

    const std::string xml = ScxmlSerializer::serialize(ir);
    EXPECT_NE(xml.find("<parallel id=\"Par\">"), std::string::npos);
    EXPECT_NE(xml.find("</parallel>"), std::string::npos);

    ScxmlParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, parsed, err)) << err.render_to_string();
    const auto* par_parsed = parsed.find_state("Par");
    ASSERT_NE(par_parsed, nullptr);
    EXPECT_EQ(par_parsed->kind, StateKind::Parallel);
    EXPECT_EQ(par_parsed->orthogonal_regions.size(), 2u);
}

/**
 * @brief Regression: W3C SCXML space-separated `event="Start Boot"` on a `<transition>` expands into separate
 * `TransitionEdge` entries for each event rather than a single `Start_Boot` event.
 * @scenario Parse `<transition event="Start Boot" target="Running"/>`.
 * @expected Two transitions (`Start` and `Boot`) targeting `Running`.
 */
TEST(ScxmlParser, SpaceSeparatedEvents_SplitIntoSeparateTransitions) {
    const std::string scxml = R"xml(
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Init">
  <state id="Init">
    <transition event="Start Boot" target="Running"/>
  </state>
  <state id="Running"/>
</scxml>
)xml";

    ScxmlParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(scxml, ir, err)) << err.render_to_string();
    ASSERT_EQ(ir.transitions.size(), 2u);
    EXPECT_EQ(ir.transitions[0].event, "Start");
    EXPECT_EQ(ir.transitions[1].event, "Boot");
    EXPECT_EQ(ir.transitions[0].target, "Running");
    EXPECT_EQ(ir.transitions[1].target, "Running");
}

/**
 * @brief Regression: Single-quoted SCXML string `<data id="msg" expr="'hello'"/>` normalizes outer
 * single quotes to double quotes (`"\"hello\""`) so C++ codegen does not emit a multi-char character literal.
 * @scenario Parse `<datamodel><data id="msg" expr="'hello'"/></datamodel>`.
 * @expected `msg` variable has `type.primitive() == PrimitiveTypeKind::String` and `initial_value == "\"hello\""`.
 */
TEST(ScxmlParser, SingleQuotedDataString_NormalizesToDoubleQuotes) {
    const std::string scxml = R"xml(
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Init">
  <datamodel>
    <data id="msg" expr="'hello'"/>
  </datamodel>
  <state id="Init"/>
</scxml>
)xml";

    ScxmlParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(scxml, ir, err)) << err.render_to_string();
    const auto* var = ir.find_variable("msg");
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->type.primitive(), PrimitiveTypeKind::String);
    EXPECT_EQ(var->initial_value, R"("hello")");
}

/**
 * @brief Regression: Multiple SCXML transitions between the same `source` and `target` states
 * (e.g. internal transitions for `Ping` and `Tick`) receive distinct synthetic action names
 * so their `<assign>` operations do not collide in C++ codegen.
 * @scenario Parse `<state id="Idle">` with two internal transitions `Ping` (`x = x + 1`) and `Tick` (`x = x + 2`).
 * @expected `transitions[0].transition_action->name != transitions[1].transition_action->name`.
 */
TEST(ScxmlParser, MultipleTransitionsSameEndpoints_GenerateDistinctSyntheticActionNames) {
    const std::string scxml = R"xml(
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Idle">
  <datamodel>
    <data id="x" expr="0"/>
  </datamodel>
  <state id="Idle">
    <transition event="Ping" type="internal">
      <assign location="x" expr="x + 1"/>
    </transition>
    <transition event="Tick">
      <assign location="x" expr="x + 2"/>
    </transition>
  </state>
</scxml>
)xml";

    ScxmlParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(scxml, ir, err)) << err.render_to_string();
    ASSERT_EQ(ir.transitions.size(), 2u);
    ASSERT_TRUE(ir.transitions[0].transition_action.has_value());
    ASSERT_TRUE(ir.transitions[1].transition_action.has_value());
    EXPECT_NE(ir.transitions[0].transition_action->name, ir.transitions[1].transition_action->name);
}

/**
 * @brief Regression: `ScxmlSerializer` must not emit a bogus `<send event="<action name>"/>` for synthesized
 * assignment actions (entry actions and transition actions that only carry assignments).
 * @scenario Parse SCXML with an `<onentry>` assignment and a transition `<assign>`, then re-serialize.
 * @expected The output keeps the `<assign>` elements and contains no `<send>` element.
 */
TEST(ScxmlSerializerTest, AssignmentOnlyActions_DoNotEmitBogusSend) {
    const std::string scxml = R"xml(
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Idle">
  <datamodel>
    <data id="x" expr="0"/>
  </datamodel>
  <state id="Idle">
    <onentry>
      <assign location="x" expr="1"/>
    </onentry>
    <transition event="Step" target="Done">
      <assign location="x" expr="x + 1"/>
    </transition>
  </state>
  <state id="Done"/>
</scxml>
)xml";

    ScxmlParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(scxml, ir, err)) << err.render_to_string();

    const std::string out = ScxmlSerializer::serialize(ir);
    EXPECT_NE(out.find("<assign location=\"x\" expr=\"x + 1\"/>"), std::string::npos) << out;
    EXPECT_EQ(out.find("<send"), std::string::npos) << out;
}

/**
 * @brief Regression: `ScxmlSerializer` preserves `model.properties` (`<!-- @fsm:property ... -->`)
 * and `state.time_invariant` (`invariant="..."`) across SCXML roundtrips.
 * @scenario Serialize an FSM with `FormalProperty("safe", "G (not Bad)")` and state `Active` (`500[ms]`), then
 * re-parse.
 * @expected Re-parsed IR keeps the property and `Active->time_invariant == "500[ms]"`.
 */
TEST(ScxmlSerializerTest, PropertiesAndTimeInvariant_PreservedAcrossRoundtrip) {
    FsmIr ir;
    ir.name = "ScxmlPropInvRt";
    ir.initial_state = "Active";
    auto& active = ir.add_state("Active");
    active.time_invariant = "500[ms]";

    FormalProperty prop;
    prop.name = "safe";
    prop.kind = PropertyKind::Safety;
    prop.raw_formula = "G (not Bad)";
    ir.add_property(prop);

    const std::string xml = ScxmlSerializer::serialize(ir);
    ScxmlParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, parsed, err)) << err.render_to_string();

    ASSERT_EQ(parsed.properties.size(), 1u);
    EXPECT_EQ(parsed.properties[0].name, "safe");
    EXPECT_EQ(parsed.properties[0].raw_formula, "G (not Bad)");

    const auto* p_active = parsed.find_state("Active");
    ASSERT_NE(p_active, nullptr);
    ASSERT_TRUE(p_active->time_invariant.has_value());
    EXPECT_EQ(*p_active->time_invariant, "500[ms]");
}

/**
 * @brief Regression: `ScxmlSerializer` and `ScxmlParser` preserve `Fork`, `Join`, `EntryPoint`,
 * `ExitPoint`, and `Final` state kinds across SCXML roundtrips.
 * @scenario Serialize an FSM with `Fork`, `Join`, `EntryPoint`, `ExitPoint`, and `Final` states, then re-parse.
 * @expected Re-parsed IR preserves all five `StateKind` values.
 */
TEST(ScxmlSerializerTest, PseudostatesAndFinalKind_PreservedAcrossRoundtrip) {
    FsmIr ir;
    ir.name = "ScxmlPseudoRt";
    ir.initial_state = "Init";
    ir.add_state("Init");
    ir.add_or_get_state("Split", "", StateKind::Fork);
    ir.add_or_get_state("Merge", "", StateKind::Join);
    ir.add_or_get_state("Done", "", StateKind::Final);
    auto& comp = ir.add_state("Comp");
    comp.is_composite = true;
    comp.initial_sub_state = "S1";
    ir.add_state("S1", "Comp");
    ir.add_or_get_state("Ep", "Comp", StateKind::EntryPoint);
    ir.add_or_get_state("Xp", "Comp", StateKind::ExitPoint);

    const std::string xml = ScxmlSerializer::serialize(ir);
    ScxmlParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, parsed, err)) << err.render_to_string();

    ASSERT_NE(parsed.find_state("Split"), nullptr);
    EXPECT_EQ(parsed.find_state("Split")->kind, StateKind::Fork);
    ASSERT_NE(parsed.find_state("Merge"), nullptr);
    EXPECT_EQ(parsed.find_state("Merge")->kind, StateKind::Join);
    ASSERT_NE(parsed.find_state("Done"), nullptr);
    EXPECT_EQ(parsed.find_state("Done")->kind, StateKind::Final);
    ASSERT_NE(parsed.find_state("Ep"), nullptr);
    EXPECT_EQ(parsed.find_state("Ep")->kind, StateKind::EntryPoint);
    ASSERT_NE(parsed.find_state("Xp"), nullptr);
    EXPECT_EQ(parsed.find_state("Xp")->kind, StateKind::ExitPoint);
}

/**
 * @brief Regression: `ScxmlSerializer` emits `event="when(<expr>)"` for `ChangeTrigger`
 * transitions so `ScxmlParser` preserves `ChangeTrigger` across SCXML roundtrips.
 * @scenario Serialize a transition with `ChangeTrigger("count > 5")` to SCXML and re-parse.
 * @expected Serialized SCXML contains `event="when(count &gt; 5)"` and re-parsed transition holds `ChangeTrigger`.
 */
TEST(ScxmlSerializerTest, ChangeTrigger_PreservedAcrossRoundtrip) {
    FsmIr ir;
    ir.name = "ScxmlChangeRt";
    ir.initial_state = "Wait";
    ir.add_state("Wait");
    ir.add_state("Done");
    ir.add_variable(VariableDefinition("count", DataType::int32(), "0"));
    TransitionEdge t("Wait", "Done", "");
    t.trigger = ChangeTrigger("count > 5", true);
    ir.add_transition(t);

    const std::string xml = ScxmlSerializer::serialize(ir);
    EXPECT_NE(xml.find("event=\"when(count &gt; 5)\""), std::string::npos) << xml;

    ScxmlParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, parsed, err)) << err.render_to_string();
    ASSERT_EQ(parsed.transitions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ChangeTrigger>(parsed.transitions[0].trigger));
    EXPECT_EQ(std::get<ChangeTrigger>(parsed.transitions[0].trigger).raw_expression, "count > 5");
}

/**
 * @brief Regression: `ScxmlSerializer` emits `kind="choice"` for `ChoiceNode` entries so
 * `ScxmlParser` preserves `choice_nodes` across SCXML roundtrips.
 * @scenario Serialize an FSM with `ChoiceNode("C1")` to SCXML and re-parse.
 * @expected Serialized SCXML contains `kind="choice"` and re-parsed IR has `is_choice_node("C1") == true`.
 */
TEST(ScxmlSerializerTest, ChoiceNode_PreservedAcrossRoundtrip) {
    FsmIr ir;
    ir.name = "ScxmlChoiceRt";
    ir.initial_state = "Init";
    ir.add_state("Init");
    ir.add_choice_node("C1");
    ir.add_state("High");
    ir.add_transition(TransitionEdge("Init", "C1", "Eval"));
    ir.add_transition(TransitionEdge("C1", "High", ""));

    const std::string xml = ScxmlSerializer::serialize(ir);
    EXPECT_NE(xml.find("kind=\"Choice\""), std::string::npos) << xml;

    ScxmlParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, parsed, err)) << err.render_to_string();
    EXPECT_TRUE(parsed.is_choice_node("C1"));
    EXPECT_FALSE(parsed.choice_nodes.empty());
}

/**
 * @brief Regression: `ScxmlSerializer` and `ScxmlParser` preserve multiple `traceability_reqs`
 * (`satisfies="REQ_001, REQ_002"`) and typed `SignalDefinition::attributes` across SCXML roundtrips.
 * @scenario Serialize an FSM with `Idle` carrying `{"REQ_001", "REQ_002"}` and signal `Telemetry{uint32 val, string
 * tag}`, then re-parse.
 * @expected Re-parsed `Idle` has both requirements and `Telemetry` preserves both payload attributes.
 */
TEST(ScxmlSerializerTest, MultiReqsAndSignalAttributes_PreservedAcrossRoundtrip) {
    FsmIr ir;
    ir.name = "ScxmlSigReqRt";
    ir.initial_state = "Idle";
    auto& idle = ir.add_state("Idle");
    idle.traceability_reqs = {"REQ_001", "REQ_002"};

    SignalDefinition tel("Telemetry");
    tel.attributes.emplace_back("val", DataType::uint32());
    tel.attributes.emplace_back("tag", DataType::string());
    tel.validators.push_back("val > 0");
    ir.add_signal(tel);

    const std::string xml = ScxmlSerializer::serialize(ir);
    ScxmlParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, parsed, err)) << err.render_to_string();

    const auto* p_idle = parsed.find_state("Idle");
    ASSERT_NE(p_idle, nullptr);
    ASSERT_EQ(p_idle->traceability_reqs.size(), 2u);
    EXPECT_EQ(p_idle->traceability_reqs[0], "REQ_001");
    EXPECT_EQ(p_idle->traceability_reqs[1], "REQ_002");

    const auto* p_tel = parsed.find_signal("Telemetry");
    ASSERT_NE(p_tel, nullptr);
    ASSERT_EQ(p_tel->attributes.size(), 2u);
    EXPECT_EQ(p_tel->attributes[0].name, "val");
    EXPECT_EQ(p_tel->attributes[1].name, "tag");
    ASSERT_EQ(p_tel->validators.size(), 1u);
    EXPECT_EQ(p_tel->validators[0], "val > 0");
}

/**
 * @brief Regression: `ScxmlSerializer` emits `type="local"` on `TransitionEdgeKind::Local`
 * transitions so `ScxmlParser` preserves `Local` kind across W3C SCXML roundtrips.
 * @scenario Serialize a composite state `Comp` with a `Local` transition `Comp -> S2`, then re-parse.
 * @expected Serialized SCXML contains `type="local"` and re-parsed transition has `kind == TransitionEdgeKind::Local`.
 */
TEST(ScxmlSerializerTest, LocalTransition_PreservedAcrossRoundtrip) {
    FsmIr ir;
    ir.name = "ScxmlLocalRt";
    ir.initial_state = "Comp";
    auto& comp = ir.add_state("Comp");
    comp.is_composite = true;
    comp.initial_sub_state = "S1";
    ir.add_state("S1", "Comp");
    ir.add_state("S2", "Comp");
    TransitionEdge t("Comp", "S2", "GoS2");
    t.kind = TransitionEdgeKind::Local;
    ir.add_transition(t);

    const std::string xml = ScxmlSerializer::serialize(ir);
    EXPECT_NE(xml.find("type=\"local\""), std::string::npos) << xml;

    ScxmlParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(xml, parsed, err)) << err.render_to_string();
    ASSERT_EQ(parsed.transitions.size(), 1u);
    EXPECT_EQ(parsed.transitions[0].kind, TransitionEdgeKind::Local);
}

}  // namespace

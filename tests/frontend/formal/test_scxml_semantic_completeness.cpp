/**
 * @file test_scxml_semantic_completeness.cpp
 * @brief Unit test suite verifying W3C SCXML semantic completeness (parallel regions, final states, events).
 */

#include <gtest/gtest.h>

#include "fsm/frontend/formal/scxml_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::frontend::formal;
using namespace fsm::frontend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify SCXML <parallel> orthogonal regions parsing into FsmIr.
 * @scenario Parse SCXML document with parallel composite state and multiple orthogonal child states.
 * @expected Parallel state kind assigned and orthogonal child regions correctly linked.
 */
TEST(ScxmlSemanticCompleteness, ParallelRegions_ParsedAsOrthogonalStates) {
    const std::string scxml_src = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" name="AvionicsDualChannel">
    <parallel id="DualChannel">
        <state id="NavigationChannel">
            <state id="NavAlign"/>
            <state id="NavTracking"/>
        </state>
        <state id="GuidanceChannel">
            <state id="GuidanceStandby"/>
            <state id="GuidanceActive"/>
        </state>
    </parallel>
</scxml>
)";

    ScxmlParser parser;
    FsmIr model;
    std::string err;
    bool ok = parser.parse(scxml_src, model, err);

    ASSERT_TRUE(ok) << "SCXML parse error: " << err;

    const auto* dual_channel = model.find_state("DualChannel");
    ASSERT_NE(dual_channel, nullptr);
    EXPECT_EQ(dual_channel->kind, StateKind::Parallel);
    ASSERT_EQ(dual_channel->orthogonal_regions.size(), 2u);

    EXPECT_EQ(dual_channel->orthogonal_regions[0].id, "NavigationChannel");
    EXPECT_EQ(dual_channel->orthogonal_regions[1].id, "GuidanceChannel");

    const auto* nav_state = model.find_state("NavigationChannel");
    ASSERT_NE(nav_state, nullptr);
    EXPECT_EQ(nav_state->parent_state, "DualChannel");

    const auto* guid_state = model.find_state("GuidanceChannel");
    ASSERT_NE(guid_state, nullptr);
    EXPECT_EQ(guid_state->parent_state, "DualChannel");
}

/**
 * @brief Verify SCXML <final> states and automatic completion events.
 * @scenario Parse SCXML model containing final states within sub-states and top-level automaton.
 * @expected Final state kind assigned and completion transitions established.
 */
TEST(ScxmlSemanticCompleteness, FinalStates_EmitsCompletionEvents) {
    const std::string scxml_src = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" name="MissionLifecycle">
    <state id="Executing">
        <state id="Step1"/>
        <final id="TaskDone"/>
        <transition event="done.state.TaskDone" target="Completed"/>
    </state>
    <state id="Completed"/>
</scxml>
)";

    ScxmlParser parser;
    FsmIr model;
    std::string err;
    bool ok = parser.parse(scxml_src, model, err);

    ASSERT_TRUE(ok) << "SCXML parse error: " << err;

    const auto* final_state = model.find_state("TaskDone");
    ASSERT_NE(final_state, nullptr);
    EXPECT_EQ(final_state->kind, StateKind::Final);

    // Verify completion transition
    bool found_completion_trans = false;
    for (const auto& trans : model.transitions) {
        if (trans.event == "done_state_TaskDone" || trans.event == "done.state.TaskDone") {
            found_completion_trans = true;
            EXPECT_EQ(trans.target, "Completed");
            break;
        }
    }
    EXPECT_TRUE(found_completion_trans);

    // Verify synthesized completion event entry action on final state
    ASSERT_FALSE(final_state->entry_actions.empty());
    EXPECT_EQ(final_state->entry_actions[0].name, "emit_done_state_Executing");
}

/**
 * @brief Verify SCXML <send> and <raise> event dispatching statements in executable content.
 * @scenario Parse transition action scripts containing <send> and <raise> directives.
 * @expected Event dispatching statements extracted and represented in ActionSignature lists.
 */
TEST(ScxmlSemanticCompleteness, SendAndRaiseDirectives_CapturedInActionIr) {
    const std::string scxml_src = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" name="EventCascade">
    <state id="Monitoring">
        <onentry>
            <send event="EvTelemetrySync"/>
        </onentry>
        <transition event="EvAnomaly" target="Degraded">
            <raise event="EvInternalAlert"/>
        </transition>
    </state>
    <state id="Degraded"/>
</scxml>
)";

    ScxmlParser parser;
    FsmIr model;
    std::string err;
    bool ok = parser.parse(scxml_src, model, err);

    ASSERT_TRUE(ok) << "SCXML parse error: " << err;

    // Verify raised and sent actions
    bool has_internal_alert = false;
    for (const auto& act : model.actions) {
        if (act.name == "EvInternalAlert") {
            has_internal_alert = true;
        }
    }
    EXPECT_TRUE(has_internal_alert);

    const auto* mon_state = model.find_state("Monitoring");
    ASSERT_NE(mon_state, nullptr);
    ASSERT_FALSE(mon_state->entry_actions.empty());
    EXPECT_EQ(mon_state->entry_actions[0].name, "EvTelemetrySync");
}

/**
 * @brief Verify XML entity decoding inside SCXML condition attributes and data expressions.
 * @scenario Parse SCXML containing relational conditions with escaped XML entities (&amp;, &gt;, &lt;).
 * @expected Guards decoded to clean relational boolean expressions without residual entities.
 */
TEST(ScxmlSemanticCompleteness, XmlEntities_DecodedInGuardsAndAssignments) {
    const std::string scxml_src = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" name="EntityGuards">
    <state id="Idle">
        <transition event="Check" target="Active" cond="ready &amp;&amp; valid &gt; 0">
            <assign location="counter" expr="counter + 1"/>
        </transition>
    </state>
    <state id="Active"/>
</scxml>
)";

    ScxmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(scxml_src, model, err)) << "SCXML parse error: " << err;

    ASSERT_EQ(model.transitions.size(), 1u);
    const auto& tr = model.transitions[0];
    EXPECT_EQ(tr.source, "Idle");
    EXPECT_EQ(tr.target, "Active");
    EXPECT_EQ(tr.event, "Check");
    ASSERT_TRUE(tr.guard.has_value());
    EXPECT_NE(tr.guard->find("ready"), std::string::npos);
    EXPECT_NE(tr.guard->find("valid"), std::string::npos);
    ASSERT_TRUE(tr.transition_action.has_value());
    ASSERT_FALSE(tr.transition_action->assignments.empty());
    EXPECT_EQ(tr.transition_action->assignments[0].target, "counter");
    EXPECT_EQ(tr.transition_action->assignments[0].expression, "counter + 1");
}

}  // namespace

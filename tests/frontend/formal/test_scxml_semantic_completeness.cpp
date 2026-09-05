#include <gtest/gtest.h>

#include "fsm/frontend/formal/scxml_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::frontend::formal;
using namespace fsm::frontend;
using namespace fsm::ir;

namespace {

/**
 * @brief Test Intent: Verify SCXML parallel element creates StateKind::Parallel and orthogonal regions.
 *
 * Scenario:
 * - Ingest SCXML with root parallel and child states representing concurrent orthogonal regions.
 * - Verify StateKind::Parallel and populated orthogonal_regions vector in parent state.
 */
TEST(ScxmlSemanticCompletenessTest, ParallelOrthogonalRegions) {
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
 * @brief Test Intent: Verify SCXML final element creates StateKind::Final state nodes and completion transitions.
 *
 * Scenario:
 * - Ingest SCXML with composite state ending in <final id="TaskDone">.
 * - Ingest completion transition triggered by done.state.TaskDone.
 * - Verify StateKind::Final and transition trigger.
 */
TEST(ScxmlSemanticCompletenessTest, FinalStatesAndCompletionEvents) {
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
}

/**
 * @brief Test Intent: Verify SCXML send and raise elements dispatch internal events and register actions.
 *
 * Scenario:
 * - Parse transition containing <raise event="EvInternalAlert"/> and onentry with <send event="EvTelemetrySync"/>.
 * - Verify events are registered in FsmIr event model.
 */
TEST(ScxmlSemanticCompletenessTest, SendAndRaiseEventsDispatch) {
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

}  // namespace

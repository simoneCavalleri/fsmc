#include <gtest/gtest.h>

#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify SysML v2 ingestion of user-defined enum definitions and literals.
 *
 * Scenario:
 * - Parse SysML v2 source containing enum def with default and custom underlying types.
 * - Verify parsed EnumDefinition models in FsmIr (names, literals, values).
 */
TEST(Sysml2StructuredDataTest, EnumDefinitionsIngestion) {
    const std::string sysml_src = R"(
package Avionics {
    state def FlightControl {
        enum def FmsOperatingMode {
            enum Standby = 0;
            enum Active = 1;
            enum Degraded = 2;
            enum Emergency;
        }

        enum def LateralMode :> uint16 {
            enum RollHold;
            enum HeadingSelect;
            enum NavTrack;
        }

        state Operational;
    }
}
)";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    bool success = parser.parse(sysml_src, model, err);

    ASSERT_TRUE(success) << "Parser error: " << err;
    ASSERT_EQ(model.enums.size(), 2u);

    // Verify FmsOperatingMode
    const auto* fms_mode = model.find_enum("FmsOperatingMode");
    ASSERT_NE(fms_mode, nullptr);
    EXPECT_EQ(fms_mode->underlying_type, "uint8_t");
    ASSERT_EQ(fms_mode->literals.size(), 4u);
    EXPECT_EQ(fms_mode->literals[0].name, "Standby");
    ASSERT_TRUE(fms_mode->literals[0].value.has_value());
    EXPECT_EQ(*fms_mode->literals[0].value, 0);
    EXPECT_EQ(fms_mode->literals[1].name, "Active");
    ASSERT_TRUE(fms_mode->literals[1].value.has_value());
    EXPECT_EQ(*fms_mode->literals[1].value, 1);
    EXPECT_EQ(fms_mode->literals[2].name, "Degraded");
    ASSERT_TRUE(fms_mode->literals[2].value.has_value());
    EXPECT_EQ(*fms_mode->literals[2].value, 2);
    EXPECT_EQ(fms_mode->literals[3].name, "Emergency");
    EXPECT_FALSE(fms_mode->literals[3].value.has_value());

    // Verify LateralMode
    const auto* lat_mode = model.find_enum("LateralMode");
    ASSERT_NE(lat_mode, nullptr);
    EXPECT_EQ(lat_mode->underlying_type, "uint16");
    ASSERT_EQ(lat_mode->literals.size(), 3u);
    EXPECT_TRUE(lat_mode->has_literal("RollHold"));
    EXPECT_TRUE(lat_mode->has_literal("HeadingSelect"));
    EXPECT_TRUE(lat_mode->has_literal("NavTrack"));
}

/**
 * @brief Test Intent: Verify SysML v2 ingestion of struct def and datatype def with physical units and initializers.
 *
 * Scenario:
 * - Parse struct def with attribute types, units, and default values.
 * - Parse datatype def and verify is_datatype flag is set.
 */
TEST(Sysml2StructuredDataTest, StructAndDatatypeDefinitionsIngestion) {
    const std::string sysml_src = R"(
package Navigation {
    state def WaypointManager {
        struct def FlightPlanWaypoint {
            attribute waypointId : uint32 = 1;
            attribute targetAltitude_ft : Real [ft] = 10000.0;
            attribute groundSpeed_kts : Real [kts] = 250.0;
            attribute isActive : Boolean = false;
        }

        datatype def AirDataState {
            attribute calibratedAirspeed : Real [m/s] = 0.0;
            attribute baroAltitude : Real [m] = 0.0;
        }

        state EnRoute;
    }
}
)";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    bool success = parser.parse(sysml_src, model, err);

    ASSERT_TRUE(success) << "Parser error: " << err;
    ASSERT_EQ(model.structs.size(), 2u);

    // Verify FlightPlanWaypoint
    const auto* wp = model.find_struct("FlightPlanWaypoint");
    ASSERT_NE(wp, nullptr);
    EXPECT_FALSE(wp->is_datatype);
    ASSERT_EQ(wp->fields.size(), 4u);
    EXPECT_EQ(wp->fields[0].name, "waypointId");
    EXPECT_EQ(wp->fields[0].type, "uint32_t");
    EXPECT_EQ(wp->fields[0].default_value, "1");

    EXPECT_EQ(wp->fields[1].name, "targetAltitude_ft");
    EXPECT_EQ(wp->fields[1].type, "float");
    EXPECT_EQ(wp->fields[1].default_value, "10000.0");
    ASSERT_TRUE(wp->fields[1].physical_unit.has_value());
    EXPECT_EQ(*wp->fields[1].physical_unit, "ft");

    EXPECT_EQ(wp->fields[2].name, "groundSpeed_kts");
    EXPECT_EQ(wp->fields[2].type, "float");
    EXPECT_EQ(wp->fields[2].default_value, "250.0");
    ASSERT_TRUE(wp->fields[2].physical_unit.has_value());
    EXPECT_EQ(*wp->fields[2].physical_unit, "kts");

    EXPECT_EQ(wp->fields[3].name, "isActive");
    EXPECT_EQ(wp->fields[3].type, "bool");
    EXPECT_EQ(wp->fields[3].default_value, "false");

    // Verify AirDataState
    const auto* ad = model.find_struct("AirDataState");
    ASSERT_NE(ad, nullptr);
    EXPECT_TRUE(ad->is_datatype);
    ASSERT_EQ(ad->fields.size(), 2u);
    EXPECT_EQ(ad->fields[0].name, "calibratedAirspeed");
    EXPECT_EQ(ad->fields[0].type, "float");
    ASSERT_TRUE(ad->fields[0].physical_unit.has_value());
    EXPECT_EQ(*ad->fields[0].physical_unit, "m/s");
}

/**
 * @brief Test Intent: Verify native temporal triggers accept after(duration) and accept at(time) in SysML v2.
 *
 * Scenario:
 * - Parse transitions with accept after 500 [ms], after(2.5 [s]), and accept at(12:00).
 * - Verify synthesized TimeTrigger objects in FsmIr transition edges.
 */
TEST(Sysml2StructuredDataTest, NativeTemporalTriggersIngestion) {
    const std::string sysml_src = R"(
package TimedMission {
    state def MissionExecutor {
        state Idle;
        state Active;
        state Degraded;
        state Scheduled;

        transition t1 first Idle accept after 500 [ms] then Active;
        transition t2 first Active accept after(2.5 [s]) then Degraded;
        transition t3 first Idle accept at(ScheduledStart) then Scheduled;
    }
}
)";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    bool success = parser.parse(sysml_src, model, err);

    ASSERT_TRUE(success) << "Parser error: " << err;
    ASSERT_EQ(model.transitions.size(), 3u);

    // Verify t1 (after 500 ms)
    const auto& t1 = model.transitions[0];
    EXPECT_EQ(t1.source, "Idle");
    EXPECT_EQ(t1.target, "Active");
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(t1.trigger));
    const auto& tt1 = std::get<TimeTrigger>(t1.trigger);
    EXPECT_EQ(tt1.kind, TimeTriggerKind::After);
    EXPECT_EQ(tt1.duration_value, 500u);
    EXPECT_EQ(tt1.unit, TimeUnit::Milliseconds);

    // Verify t2 (after 2.5 s -> 2500 ms)
    const auto& t2 = model.transitions[1];
    EXPECT_EQ(t2.source, "Active");
    EXPECT_EQ(t2.target, "Degraded");
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(t2.trigger));
    const auto& tt2 = std::get<TimeTrigger>(t2.trigger);
    EXPECT_EQ(tt2.kind, TimeTriggerKind::After);
    EXPECT_EQ(tt2.duration_value, 2500u);

    // Verify t3 (at ScheduledStart)
    const auto& t3 = model.transitions[2];
    EXPECT_EQ(t3.source, "Idle");
    EXPECT_EQ(t3.target, "Scheduled");
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(t3.trigger));
    const auto& tt3 = std::get<TimeTrigger>(t3.trigger);
    EXPECT_EQ(tt3.kind, TimeTriggerKind::At);
    EXPECT_EQ(tt3.dynamic_expression, "ScheduledStart");
}

/**
 * @brief Test Intent: Verify fork, join, entry point, and exit point pseudostates in SysML v2.
 *
 * Scenario:
 * - Parse SysML v2 containing fork, join, entry point, and exit point pseudostates.
 * - Verify StateKind attributes in FsmIr state nodes.
 */
TEST(Sysml2StructuredDataTest, ConnectionPseudostatesIngestion) {
    const std::string sysml_src = R"(
package Topology {
    state def CompositeMachine {
        state RootComposite {
            entry point Ingress;
            exit point Egress;
            fork ForkBranch;
            join JoinRendezvous;

            state WorkerA;
            state WorkerB;
        }
    }
}
)";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    bool success = parser.parse(sysml_src, model, err);

    ASSERT_TRUE(success) << "Parser error: " << err;

    const auto* ingress = model.find_state("Ingress");
    ASSERT_NE(ingress, nullptr);
    EXPECT_EQ(ingress->kind, StateKind::EntryPoint);

    const auto* egress = model.find_state("Egress");
    ASSERT_NE(egress, nullptr);
    EXPECT_EQ(egress->kind, StateKind::ExitPoint);

    const auto* fork_node = model.find_state("ForkBranch");
    ASSERT_NE(fork_node, nullptr);
    EXPECT_EQ(fork_node->kind, StateKind::Fork);

    const auto* join_node = model.find_state("JoinRendezvous");
    ASSERT_NE(join_node, nullptr);
    EXPECT_EQ(join_node->kind, StateKind::Join);
}

}  // namespace

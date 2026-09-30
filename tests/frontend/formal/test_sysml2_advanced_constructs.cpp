/**
 * @file test_sysml2_advanced_constructs.cpp
 * @brief Unit test suite for SysML v2 advanced constructs (nested/qualified packages,
 *        binding connectors, fork and join composite transitions).
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/formal/sysml2_serializer.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"

using namespace fsm::backend::formal;
using namespace fsm::frontend::formal;
using namespace fsm::middleend::analysis;
using namespace fsm::ir;

namespace {

TEST(Sysml2AdvancedConstructs, QualifiedPackage_ParsedIntoModelPackage) {
    const std::string sysml_text = R"(
    package Aerospace::Navigation {
        state def Autopilot {
            entry; then Idle;
            state Idle;
        }
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    EXPECT_EQ(model.package, "Aerospace::Navigation");
    EXPECT_EQ(model.name, "Autopilot");
    EXPECT_EQ(model.initial_state, "Idle");
}

TEST(Sysml2AdvancedConstructs, NestedPackage_ConcatenatesHierarchy) {
    const std::string sysml_text = R"(
    package Spacecraft {
        package Propulsion {
            package ThrustController {
                state def MainEngine {
                    entry; then Standby;
                    state Standby;
                }
            }
        }
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    EXPECT_EQ(model.package, "Spacecraft::Propulsion::ThrustController");
    EXPECT_EQ(model.name, "MainEngine");
    EXPECT_EQ(model.initial_state, "Standby");
}

TEST(Sysml2AdvancedConstructs, BindingConnectors_PopulatesBindingsAndPortBoundTo) {
    const std::string sysml_text = R"(
    package Telemetry {
        state def SensorHub {
            in port raw_temp : Real;
            out port filtered_temp : Real;

            bind raw_temp = adc.channel0;
            connect filtered_temp to bus.tx_temp;

            entry; then Running;
            state Running;
        }
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    EXPECT_EQ(model.package, "Telemetry");
    EXPECT_EQ(model.name, "SensorHub");

    // Check bindings on the FsmIr model
    ASSERT_EQ(model.bindings.size(), 2u);
    EXPECT_EQ(model.bindings[0].destination, "raw_temp");
    EXPECT_EQ(model.bindings[0].source, "adc.channel0");

    EXPECT_EQ(model.bindings[1].destination, "filtered_temp");
    EXPECT_EQ(model.bindings[1].source, "bus.tx_temp");

    // Check bound_to property propagated to PortDefinition
    ASSERT_EQ(model.ports.size(), 2u);
    auto it_in =
        std::find_if(model.ports.begin(), model.ports.end(), [](const auto& p) { return p.name == "raw_temp"; });
    ASSERT_NE(it_in, model.ports.end());
    EXPECT_EQ(it_in->bound_to, "adc.channel0");

    auto it_out =
        std::find_if(model.ports.begin(), model.ports.end(), [](const auto& p) { return p.name == "filtered_temp"; });
    ASSERT_NE(it_out, model.ports.end());
    EXPECT_EQ(it_out->bound_to, "bus.tx_temp");
}

TEST(Sysml2AdvancedConstructs, ForkCompositeTransition_ParsesMultipleTargets) {
    const std::string sysml_text = R"(
    state def ParallelExecution {
        entry; then Idle;
        state Idle;
        state TaskA;
        state TaskB;

        transition fork_tasks
            first Idle
            accept StartTrigger
            fork (TaskA, TaskB);
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "ParallelExecution");
    ASSERT_EQ(model.transitions.size(), 1u);

    const auto& tx = model.transitions[0];
    EXPECT_EQ(tx.source, "Idle");
    EXPECT_EQ(tx.event, "StartTrigger");

    std::vector<std::string> expected_targets = {"TaskA", "TaskB"};
    EXPECT_EQ(tx.target_ids, expected_targets);
    ASSERT_EQ(tx.multi_target_ids.size(), 2u);
    EXPECT_EQ(tx.target, "TaskA");
}

TEST(Sysml2AdvancedConstructs, JoinCompositeTransition_ParsesMultipleSources) {
    const std::string sysml_text = R"(
    state def ParallelExecution {
        entry; then TaskA;
        state TaskA;
        state TaskB;
        state Done;

        transition join_tasks
            join (TaskA, TaskB)
            accept CompleteTrigger
            then Done;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "ParallelExecution");
    ASSERT_EQ(model.transitions.size(), 1u);

    const auto& tx = model.transitions[0];
    EXPECT_EQ(tx.target, "Done");
    EXPECT_EQ(tx.event, "CompleteTrigger");

    std::vector<std::string> expected_sources = {"TaskA", "TaskB"};
    EXPECT_EQ(tx.source_ids, expected_sources);
    ASSERT_EQ(tx.multi_source_ids.size(), 2u);
    EXPECT_EQ(tx.source, "TaskA");
}

/**
 * @brief Verify SysML v2 parallel states and lifecycle slash action syntax (entry / act; exit / act;)
 *        are parsed accurately and serialized losslessly without reachability anomalies.
 */
TEST(Sysml2AdvancedConstructs, ParallelStateAndSlashActions_RoundtrippedLosslessly) {
    const std::string sysml_text = R"(
    state def ConcurrentSystem {
        entry; then Standby;

        state Standby;

        state Shutdown {
            entry / OpenContactorsAction;
            exit / ResetRelaysAction;
        }

        parallel state Operational {
            state ThermalRegion {
                entry; then CoolingOff;
                state CoolingOff;
                state CoolingOn;
                transition from CoolingOff accept TempHigh then CoolingOn;
            }
            state PressureRegion {
                entry; then PressureNominal;
                state PressureNominal;
                state PressureVent;
                transition from PressureNominal accept Overpressure then PressureVent;
            }
        }

        transition from Standby accept StartCmd then Operational;
        transition from Operational accept EStopCmd then Shutdown;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    // Verify Shutdown actions
    const auto* shutdown_st = model.find_state("Shutdown");
    ASSERT_NE(shutdown_st, nullptr);
    ASSERT_FALSE(shutdown_st->entry_actions.empty());
    EXPECT_EQ(shutdown_st->entry_actions[0].name, "OpenContactorsAction");
    ASSERT_FALSE(shutdown_st->exit_actions.empty());
    EXPECT_EQ(shutdown_st->exit_actions[0].name, "ResetRelaysAction");

    // Verify Operational parallel state
    const auto* op_st = model.find_state("Operational");
    ASSERT_NE(op_st, nullptr);
    EXPECT_EQ(op_st->kind, StateKind::Parallel);

    // Initial validation must pass
    auto val1 = FsmValidator::validate(model);
    EXPECT_TRUE(val1.is_valid);
    EXPECT_TRUE(val1.errors.empty());

    // Roundtrip serialize to SysML v2
    std::string exported_sysml = Sysml2Serializer::serialize(model);
    EXPECT_NE(exported_sysml.find("parallel state Operational"), std::string::npos);
    EXPECT_NE(exported_sysml.find("entry action OpenContactorsAction;"), std::string::npos);
    EXPECT_NE(exported_sysml.find("exit action ResetRelaysAction;"), std::string::npos);

    // Parse exported model back
    FsmIr roundtrip_model;
    ASSERT_TRUE(parser.parse(exported_sysml, roundtrip_model, err)) << "Parse back error: " << err;

    const auto* rt_op = roundtrip_model.find_state("Operational");
    ASSERT_NE(rt_op, nullptr);
    EXPECT_EQ(rt_op->kind, StateKind::Parallel);

    // Validating the roundtripped model must NOT produce reachability warnings on parallel regions!
    auto val2 = FsmValidator::validate(roundtrip_model);
    EXPECT_TRUE(val2.is_valid);
    EXPECT_TRUE(val2.errors.empty());

    bool has_reachability_warning = false;
    for (const auto& w : val2.warnings) {
        if (w.find("unreachable") != std::string::npos) {
            has_reachability_warning = true;
        }
    }
    EXPECT_FALSE(has_reachability_warning);
}

}  // namespace

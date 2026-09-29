/**
 * @file test_sysml2_advanced_constructs.cpp
 * @brief Unit test suite for SysML v2 advanced constructs (nested/qualified packages,
 *        binding connectors, fork and join composite transitions).
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::frontend::formal;
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

}  // namespace

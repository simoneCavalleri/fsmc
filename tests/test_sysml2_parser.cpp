#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/fsm_validator.hpp"

using namespace fsm::codegen;

namespace {

TEST(Sysml2ParserTest, MultilineTransitionParsing) {
    const std::string sysml_text = R"(
    state def MissionBehavior {
        entry; then Standby;

        state Standby;
        state InFlight;

        transition authorize_mission
            first Standby
            accept AuthorizeCmd
            if ValidClearanceGuard
            do ArmEnginesAction
            then InFlight;

        transition abort_mission
            first Standby
            accept AuthorizeCmd
            if NoClearanceGuard
            do TriggerAlarmAction
            then Aborted;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "MissionBehavior");
    EXPECT_EQ(model.initial_state, "Standby");
    ASSERT_NE(model.find_state("Standby"), nullptr);
    ASSERT_NE(model.find_state("InFlight"), nullptr);
    ASSERT_NE(model.find_state("Aborted"), nullptr);
    EXPECT_EQ(model.events.size(), 1u);
    EXPECT_EQ(model.guards.size(), 2u);
    EXPECT_EQ(model.actions.size(), 2u);
    EXPECT_EQ(model.transitions.size(), 2u);
}

TEST(Sysml2ParserTest, CompactTransitionParsing) {
    const std::string sysml_text = R"(
    state def DeviceProtocol {
        entry; then Disconnected;

        state Disconnected;
        state Connecting;
        state Connected;

        transition from Disconnected accept ConnectCmd then Connecting;
        transition from Connecting accept SuccessEvent do OnConnectedAction then Connected;
        transition from Connecting accept FailEvent then Disconnected;
        transition from Connected accept DisconnectCmd then Disconnected;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    EXPECT_EQ(model.initial_state, "Disconnected");
    EXPECT_EQ(model.states.size(), 3u);
    EXPECT_EQ(model.transitions.size(), 4u);
    EXPECT_EQ(model.actions.size(), 1u);
}

TEST(Sysml2ParserTest, CompositeStatesAndCodegen) {
    const std::string sysml_text = R"(
    state def Spacecraft {
        entry; then Standby;

        state Standby {
            entry; then Diagnostics;
            state Diagnostics;
            state Calibrated;
        }

        state InFlight {
            entry; then Ascending;
            state Ascending;
            state Cruising;
        }

        transition from Calibrated accept AuthorizeCmd then Ascending;
        transition from Ascending accept AltitudeReachedEvent do DeployPanelsAction then Cruising;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err;

    const auto* standby = model.find_state("Standby");
    ASSERT_NE(standby, nullptr);
    EXPECT_TRUE(standby->is_composite);
    EXPECT_EQ(standby->initial_sub_state, "Diagnostics");

    const auto* diag = model.find_state("Diagnostics");
    ASSERT_NE(diag, nullptr);
    EXPECT_EQ(diag->parent_state, "Standby");

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    opts.standalone = true;

    const std::string code = CppGenerator::generate_header(model, opts);
    EXPECT_FALSE(code.empty());
    EXPECT_NE(code.find("struct Diagnostics"), std::string::npos);
    EXPECT_NE(code.find("struct DeployPanelsAction"), std::string::npos);
}

}  // namespace

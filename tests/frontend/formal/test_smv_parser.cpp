#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/formal/smv_parser.hpp"
#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify formal nuXmv / SMV parsing of states, events, and transitions.
 */
TEST(SmvParserTest, BasicSmvParsing) {
    const std::string smv_content = R"(-- nuXmv / SMV Formal Model: ConnectionControllerFSM
MODULE main

VAR
  state : {Disconnected, Connecting, Connected};
  event : {none, ConnectCmd, HandshakeOkEvent, DisconnectCmd};

ASSIGN
  init(state) := Disconnected;

  next(state) := case
    state = Disconnected & event = ConnectCmd : Connecting;
    state = Connecting & event = HandshakeOkEvent : Connected;
    state = Connected & event = DisconnectCmd : Disconnected;
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "ConnectionControllerFSM");
    EXPECT_EQ(model.initial_state, "Disconnected");
    EXPECT_EQ(model.states.size(), 3u);
    EXPECT_EQ(model.events.size(), 3u);
    EXPECT_EQ(model.transitions.size(), 3u);

    EXPECT_EQ(parser.kind(), FrontendKind::Formal);
    EXPECT_EQ(parser.format_name(), "smv");
}

/**
 * @brief Test Intent: Verify SMV variable ranges and initial assignments.
 */
TEST(SmvParserTest, SmvVariablesAndInit) {
    const std::string smv_content = R"(MODULE TrafficLight
VAR
  state : {Red, Yellow, Green};
  event : {none, TimerTick, ManualOverride};
  timer_count : 0..100;
  emergency_active : boolean;

ASSIGN
  init(state) := Red;
  init(timer_count) := 0;
  init(emergency_active) := false;

  next(state) := case
    state = Red & event = TimerTick : Green;
    state = Green & event = TimerTick : Yellow;
    state = Yellow & event = TimerTick : Red;
    state = Red & event = ManualOverride : Yellow;
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "TrafficLight");
    EXPECT_EQ(model.initial_state, "Red");
    EXPECT_EQ(model.states.size(), 3u);
    EXPECT_EQ(model.variables.size(), 2u);

    const auto* tc = model.find_variable("timer_count");
    ASSERT_NE(tc, nullptr);
    EXPECT_EQ(tc->type_kind, VariableTypeKind::Integer);
    EXPECT_EQ(tc->initial_value, "0");

    const auto* ea = model.find_variable("emergency_active");
    ASSERT_NE(ea, nullptr);
    EXPECT_EQ(ea->type_kind, VariableTypeKind::Boolean);
    EXPECT_EQ(ea->initial_value, "false");
}

/**
 * @brief Test Intent: Verify SMV temporal specifications (LTLSPEC and INVARSPEC).
 */
TEST(SmvParserTest, SmvLtlAndInvariants) {
    const std::string smv_content = R"(MODULE SafetyMonitor
VAR
  state : {Safe, Warning, Critical};
  event : {none, SensorAlert, FaultClear};

ASSIGN
  init(state) := Safe;

  next(state) := case
    state = Safe & event = SensorAlert : Warning;
    state = Warning & event = SensorAlert : Critical;
    state = Warning & event = FaultClear : Safe;
    TRUE : state;
  esac;

LTLSPEC G (state = Critical -> F (state = Safe));
INVARSPEC !(state = Critical & event = SensorAlert);
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.properties.size(), 2u);
    EXPECT_EQ(model.properties[0].kind, PropertyKind::Liveness);
    EXPECT_EQ(model.properties[0].raw_formula, "G (state = Critical -> F (state = Safe))");
    EXPECT_EQ(model.properties[1].kind, PropertyKind::Invariant);
    EXPECT_EQ(model.properties[1].raw_formula, "!(state = Critical & event = SensorAlert)");
}

/**
 * @brief Test Intent: Verify C++20 code generation from SMV-parsed model.
 */
TEST(SmvParserTest, SmvCodegenCompatibility) {
    const std::string smv_content = R"(MODULE MotorFSM
VAR
  state : {Idle, Running};
  event : {none, StartMotor, StopMotor};

ASSIGN
  init(state) := Idle;

  next(state) := case
    state = Idle & event = StartMotor : Running;
    state = Running & event = StopMotor : Idle;
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    opts.standalone = true;
    const std::string header = CppGenerator::generate_header(model, opts);
    EXPECT_FALSE(header.empty());
    EXPECT_NE(header.find("struct Idle"), std::string::npos);
    EXPECT_NE(header.find("struct Running"), std::string::npos);
    EXPECT_NE(header.find("struct StartMotor"), std::string::npos);
}

/**
 * @brief Test Intent: Error handling for invalid/empty SMV content.
 */
TEST(SmvParserTest, NegativeErrorHandling) {
    SmvParser parser;
    FsmIr model;
    std::string err;
    EXPECT_FALSE(parser.parse("", model, err));
    EXPECT_FALSE(err.empty());
}

}  // namespace

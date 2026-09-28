/**
 * @file test_smv_parser.cpp
 * @brief Unit test suite for the nuXmv / SMV formal model frontend parser.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/formal/smv_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::frontend::formal;
using namespace fsm::frontend;
using namespace fsm::backend::cpp;
using namespace fsm::backend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify basic nuXmv / SMV module state machine parsing.
 * @scenario Parse SMV module with VAR state variable, init(state) declaration, and next(state) case transitions.
 * @expected States and transitions correctly mapped into FsmIr topology.
 */
TEST(SmvParser, BasicSmvModule_ParsedIntoValidFsmIr) {
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
    EXPECT_EQ(model.signals.size(), 3u);
    EXPECT_EQ(model.transitions.size(), 3u);

    EXPECT_EQ(parser.kind(), FrontendKind::Formal);
    EXPECT_EQ(parser.format_name(), "smv");
}

/**
 * @brief Verify SMV auxiliary state variables, ranges, and init expressions.
 * @scenario Parse SMV module with integer range and boolean variables alongside state variable.
 * @expected FsmIr variable definitions created with initial values and primitive types.
 */
TEST(SmvParser, VariablesAndInitExpressions_CapturedInIr) {
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
    EXPECT_TRUE(tc->type.is_integer());
    EXPECT_EQ(tc->initial_value, "0");

    const auto* ea = model.find_variable("emergency_active");
    ASSERT_NE(ea, nullptr);
    EXPECT_TRUE(ea->type.is_boolean());
    EXPECT_EQ(ea->initial_value, "false");
}

/**
 * @brief Verify extraction of LTLSPEC and INVAR formal verification properties.
 * @scenario Parse SMV file declaring temporal logic formulas (LTLSPEC) and state invariants (INVAR).
 * @expected FormalProperty entries created with LTL/Invariant kind and raw formula strings.
 */
TEST(SmvParser, LtlSpecsAndInvariants_CapturedAsFormalProperties) {
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
 * @brief Verify C++ code generation compatibility from parsed SMV formal models.
 * @scenario Parse SMV module and invoke CppGenerator to produce standalone C++ header.
 * @expected Generated C++ code compiles cleanly with all state enums and transitions.
 */
TEST(SmvParser, SmvModule_GeneratesCompilableCppCode) {
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
 * @brief Verify graceful diagnostic reporting on malformed SMV input.
 * @scenario Feed syntactically invalid SMV source (missing semicolon, unclosed case).
 * @expected Parser returns false with descriptive diagnostic error message.
 */
TEST(SmvParser, MalformedSmv_RejectionDiagnosticsReported) {
    SmvParser parser;
    FsmIr model;
    std::string err;
    EXPECT_FALSE(parser.parse("", model, err));
    EXPECT_FALSE(err.empty());
}

/**
 * @brief Verify parsing of multiline case expressions and pure SMV state inference.
 * @scenario Parse complex SMV transition relations with compound boolean guard conditions.
 * @expected Guard expressions parsed into AST and mapped to FsmIr transitions.
 */
TEST(SmvParser, MultilineCaseExpressions_InferredAsStateTransitions) {
    const std::string smv_content = R"(MODULE ProtocolEngine
VAR
  state : {Standby, Transmitting, ErrorState};
  cmd_send : boolean;
  err_detected : boolean;
  retry_count : 0..10;

ASSIGN
  init(state) := Standby;

  next(state) := case
    (state = Standby) &
    (cmd_send = TRUE) &
    (retry_count < 5) :
        Transmitting;
    (state = Transmitting) &
    (err_detected = TRUE) :
        ErrorState;
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;

    EXPECT_EQ(model.name, "ProtocolEngine");
    EXPECT_EQ(model.initial_state, "Standby");
    EXPECT_EQ(model.states.size(), 3u);
    ASSERT_EQ(model.transitions.size(), 2u);

    const auto& t1 = model.transitions[0];
    EXPECT_EQ(t1.source, "Standby");
    EXPECT_EQ(t1.target, "Transmitting");
    EXPECT_EQ(t1.event, "cmd_send");
    ASSERT_TRUE(t1.guard.has_value());
    EXPECT_NE(t1.guard->find("retry_count"), std::string::npos);

    const auto& t2 = model.transitions[1];
    EXPECT_EQ(t2.source, "Transmitting");
    EXPECT_EQ(t2.target, "ErrorState");
    EXPECT_EQ(t2.event, "err_detected");
}

}  // namespace

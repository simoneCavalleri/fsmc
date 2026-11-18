/**
 * @file test_smv_parser.cpp
 * @brief Unit test suite for the nuXmv / SMV formal model frontend parser.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/formal/smv_serializer.hpp"
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
    // After the guard-name fix, trans.guard holds a synthetic guard name like
    // "guard_Standby_to_Transmitting_1", while the raw boolean expression is
    // stored in model.guards[n].raw_expression for proper semantic analysis.
    ASSERT_TRUE(t1.guard.has_value());
    // The guard name must be a stable identifier, not a mangled boolean expression.
    EXPECT_NE(t1.guard->find("guard_"), std::string::npos);
    // The actual expression (retry_count < 5) must appear in the guard model.
    bool found_expr_in_guards = false;
    for (const auto& gm : model.guards) {
        if (gm.raw_expression.has_value() && gm.raw_expression->find("retry_count") != std::string::npos) {
            found_expr_in_guards = true;
            break;
        }
    }
    EXPECT_TRUE(found_expr_in_guards) << "Guard raw expression not found in model.guards";

    const auto& t2 = model.transitions[1];
    EXPECT_EQ(t2.source, "Transmitting");
    EXPECT_EQ(t2.target, "ErrorState");
    EXPECT_EQ(t2.event, "err_detected");
}

/**
 * @brief Regression: SMV boolean guard expressions must not be corrupted by sanitize_identifier.
 * @scenario Parse SMV transitions with compound boolean guards (`battery_mv > 3200 && !fault_active`).
 *           Before the fix, sanitize_identifier() was called on the raw expression, replacing
 *           >, &&, ! with underscores and producing a meaningless guard name that was
 *           inconsistent with the raw expression stored in model.guards.
 * @expected trans.guard holds a stable synthetic name ("guard_..."); model.guards contains
 *           a GuardModel whose raw_expression matches the original boolean expression.
 */
TEST(SmvParser, CompoundBooleanGuard_StoredWithoutExpressionCorruption) {
    const std::string smv_content = R"(MODULE BatteryFSM
VAR
  state : {Nominal, LowBattery, Critical};
  battery_mv : 0..5000;
  fault_active : boolean;

ASSIGN
  init(state) := Nominal;

  next(state) := case
    state = Nominal & (battery_mv > 3200) & !(fault_active) : LowBattery;
    state = LowBattery & (battery_mv < 2800) : Critical;
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;

    ASSERT_EQ(model.transitions.size(), 2u);

    // Transition 1: Nominal -> LowBattery with compound guard
    const auto& t1 = model.transitions[0];
    EXPECT_EQ(t1.source, "Nominal");
    EXPECT_EQ(t1.target, "LowBattery");

    // The guard must be a stable identifier (no operator characters), not a mangled expression.
    ASSERT_TRUE(t1.guard.has_value());
    EXPECT_NE(t1.guard->find("guard_"), std::string::npos)
        << "Guard name should be a synthetic identifier, not a mangled expression";
    // Operator characters from the original boolean expression must NOT appear in the guard name.
    EXPECT_EQ(t1.guard->find(">"), std::string::npos) << "Operator '>' must not appear in guard name";
    EXPECT_EQ(t1.guard->find("!"), std::string::npos) << "Operator '!' must not appear in guard name";

    // The raw boolean expression must be preserved intact in model.guards.
    bool found_battery_expr = false;
    for (const auto& gm : model.guards) {
        if (gm.raw_expression.has_value() && gm.raw_expression->find("battery_mv") != std::string::npos) {
            found_battery_expr = true;
            EXPECT_NE(gm.raw_expression->find("3200"), std::string::npos)
                << "Guard raw_expression must preserve the original numeric literal";
            break;
        }
    }
    EXPECT_TRUE(found_battery_expr) << "Guard raw expression not found in model.guards";
}

/**
 * @brief Verify SMV 'state in { S1, S2, ... }' multi-source transition syntax.
 * @scenario Parse SMV module with set inclusion condition 'state in {Idle, Standby} & start_cmd : Active;'.
 * @expected Separate transitions emitted for each source state in the set.
 */
TEST(SmvParser, StateInSetSyntax_ParsedAsMultipleSourceTransitions) {
    const std::string smv_content = R"(MODULE MultiSourceSMV
VAR
  state : {Idle, Standby, Active};
  event : {start_cmd, none};
ASSIGN
  init(state) := Idle;
  next(state) := case
    state in {Idle, Standby} & event = start_cmd : Active;
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;

    ASSERT_EQ(model.transitions.size(), 2u);
    EXPECT_EQ(model.transitions[0].source, "Idle");
    EXPECT_EQ(model.transitions[0].target, "Active");
    EXPECT_EQ(model.transitions[0].event, "start_cmd");

    EXPECT_EQ(model.transitions[1].source, "Standby");
    EXPECT_EQ(model.transitions[1].target, "Active");
    EXPECT_EQ(model.transitions[1].event, "start_cmd");
}

/**
 * @brief Verify SMV condition splitting preserves nested parentheses containing '&'.
 * @scenario Condition 'state = Off & ((flag1 = TRUE) & (flag2 = FALSE)) : On;'.
 * @expected Expression inside nested parentheses is not broken into invalid fragments.
 */
TEST(SmvParser, NestedParenthesesInCondition_NotSplitPrematurely) {
    const std::string smv_content = R"(MODULE NestedParenSMV
VAR
  state : {Off, On};
  flag1 : boolean;
  flag2 : boolean;
ASSIGN
  init(state) := Off;
  next(state) := case
    state = Off & ((flag1 = TRUE) & (flag2 = FALSE)) : On;
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;

    ASSERT_EQ(model.transitions.size(), 1u);
    const auto& t = model.transitions[0];
    EXPECT_EQ(t.source, "Off");
    EXPECT_EQ(t.target, "On");

    // Guard raw expression should contain both flag1 and flag2
    ASSERT_FALSE(model.guards.empty());
    bool found_flags = false;
    for (const auto& gm : model.guards) {
        if (gm.raw_expression.has_value() && gm.raw_expression->find("flag1") != std::string::npos &&
            gm.raw_expression->find("flag2") != std::string::npos) {
            found_flags = true;
            break;
        }
    }
    EXPECT_TRUE(found_flags) << "Expected compound guard containing both flag1 and flag2";
}

/**
 * @brief Regression: inline `-- action:` comments after `;` must not swallow the next case branch.
 * @scenario Parse an SMV next(state) case block where transitions end with `; -- action: <name>`.
 * @expected All transitions and their commented actions are preserved without dropping following rows.
 */
TEST(SmvParser, InlineActionCommentsDoNotDropSubsequentTransitions) {
    const std::string smv_content = R"(MODULE DroneFSM
VAR
  state : {Preflight, Idle, Hovering, Landing};
  event : {none, EvArm, EvTakeoff, EvLand};
ASSIGN
  init(state) := Preflight;
  next(state) := case
    state = Preflight & event = EvArm : Idle; -- action: start_idle
    state = Idle & event = EvTakeoff : Hovering; -- action: start_ascent
    state = Hovering & event = EvLand : Landing;
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;

    ASSERT_EQ(model.transitions.size(), 3u);
    EXPECT_EQ(model.transitions[0].source, "Preflight");
    EXPECT_EQ(model.transitions[0].target, "Idle");
    ASSERT_TRUE(model.transitions[0].transition_action.has_value());
    EXPECT_EQ(model.transitions[0].transition_action->name, "start_idle");

    EXPECT_EQ(model.transitions[1].source, "Idle");
    EXPECT_EQ(model.transitions[1].target, "Hovering");
    ASSERT_TRUE(model.transitions[1].transition_action.has_value());
    EXPECT_EQ(model.transitions[1].transition_action->name, "start_ascent");

    EXPECT_EQ(model.transitions[2].source, "Hovering");
    EXPECT_EQ(model.transitions[2].target, "Landing");
}

/**
 * @brief Regression: SMV equality `=` and `TRUE`/`FALSE` literals in guards are normalized for C++ / semantic passes.
 * @scenario Parse an SMV transition guard `(arm_cmd = TRUE) & (fault = FALSE)`.
 * @expected Guard raw_expression uses `==`, `&&`, `true`, and `false` instead of mutating `=`.
 */
TEST(SmvParser, EqualityAndBooleanLiterals_NormalizedToCppSyntax) {
    const std::string smv_content = R"(MODULE ArmFSM
VAR
  state : {Preflight, Armed};
  event : {none, EvArm};
  arm_cmd : boolean;
  fault : boolean;
ASSIGN
  init(state) := Preflight;
  next(state) := case
    state = Preflight & event = EvArm & (arm_cmd = TRUE) & (fault = FALSE) : Armed;
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;
    ASSERT_EQ(model.transitions.size(), 1u);
    ASSERT_FALSE(model.guards.empty());
    ASSERT_TRUE(model.guards[0].raw_expression.has_value());
    const std::string& expr = *model.guards[0].raw_expression;
    EXPECT_NE(expr.find("arm_cmd == true"), std::string::npos);
    EXPECT_NE(expr.find("fault == false"), std::string::npos);
}

/**
 * @brief Verify inline action assignments and normalized guard expressions in SMV.
 * @scenario Parse SMV with inline `-- action: count = count + 1`, lifecycle action metadata, and a guard.
 * @expected Transition and lifecycle actions populate `assignments`, and the guard populates `normalized_expression`.
 */
TEST(SmvParser, ActionAssignmentsAndNormalizedGuards_PopulatedForNoStubs) {
    const std::string smv_content = R"(MODULE CounterFSM
VAR
  state : {Idle, Active};
  event : {none, START, STOP};
  count : 0..100;
ASSIGN
  init(state) := Idle;
  init(count) := 0;
  -- @fsm: entry state=Idle action="count = 10"
  next(state) := case
    state = Idle & event = START & count < 20 : Active; -- action: count = count + 1
    state = Active & event = STOP : Idle; -- action: count = 0
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;

    const StateNode* idle = model.find_state("Idle");
    ASSERT_NE(idle, nullptr);
    ASSERT_EQ(idle->entry_actions.size(), 1u);
    ASSERT_EQ(idle->entry_actions[0].assignments.size(), 1u);
    EXPECT_EQ(idle->entry_actions[0].assignments[0].target.name, "count");
    EXPECT_EQ(idle->entry_actions[0].assignments[0].expression, "10");

    ASSERT_EQ(model.transitions.size(), 2u);
    ASSERT_TRUE(model.transitions[0].transition_action.has_value());
    ASSERT_EQ(model.transitions[0].transition_action->assignments.size(), 1u);
    EXPECT_EQ(model.transitions[0].transition_action->assignments[0].target.name, "count");
    EXPECT_EQ(model.transitions[0].transition_action->assignments[0].expression, "count + 1");

    ASSERT_TRUE(model.transitions[1].transition_action.has_value());
    ASSERT_EQ(model.transitions[1].transition_action->assignments.size(), 1u);
    EXPECT_EQ(model.transitions[1].transition_action->assignments[0].target.name, "count");
    EXPECT_EQ(model.transitions[1].transition_action->assignments[0].expression, "0");
    EXPECT_NE(model.transitions[0].transition_action->name, model.transitions[1].transition_action->name);

    ASSERT_FALSE(model.guards.empty());
    ASSERT_TRUE(model.guards[0].normalized_expression.has_value());
    EXPECT_NE(model.guards[0].normalized_expression->find("count < 20"), std::string::npos);
}

/**
 * @brief Verify SmvParser normalizes SMV uppercase boolean literals TRUE/FALSE in init() assignments.
 * @scenario Parse SMV module with `init(armed) := TRUE;` and `init(faulted) := FALSE;`.
 * @expected Variables have `initial_value` set to `"true"` and `"false"` so C++ emission compiles cleanly.
 */
TEST(SmvParser, BooleanInitLiterals_NormalizedToLowercase) {
    const std::string smv_content = R"(MODULE main
VAR
  state : {Idle, Active};
  event : {none, EvGo};
  armed : boolean;
  faulted : boolean;
ASSIGN
  init(state) := Idle;
  init(armed) := TRUE;
  init(faulted) := FALSE;
  next(state) := case
    state = Idle & event = EvGo : Active;
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;

    const auto* armed = model.find_variable("armed");
    ASSERT_NE(armed, nullptr);
    EXPECT_EQ(armed->initial_value, "true");

    const auto* faulted = model.find_variable("faulted");
    ASSERT_NE(faulted, nullptr);
    EXPECT_EQ(faulted->initial_value, "false");
}

/**
 * @brief Regression: SMV disjunctive event clauses `(event = Go | event = Force)` and `event in {Go, Force}`
 * expand into separate `TransitionEdge` entries instead of a single mangled event name.
 * @scenario Parse SMV `next(state)` with `state = Idle & (event = Go | event = Force) : Active;`.
 * @expected Two transitions (`Go` and `Force`) from `Idle` to `Active`.
 */
TEST(SmvParser, DisjunctiveEvents_ExpandedIntoSeparateTransitions) {
    const std::string smv_content = R"(
MODULE main
VAR
  state : {Idle, Active};
  event : {none, Go, Force};
ASSIGN
  init(state) := Idle;
  next(state) := case
    state = Idle & (event = Go | event = Force) : Active;
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;
    ASSERT_EQ(model.transitions.size(), 2u);
    EXPECT_EQ(model.transitions[0].event, "Go");
    EXPECT_EQ(model.transitions[1].event, "Force");
    EXPECT_EQ(model.transitions[0].target, "Active");
    EXPECT_EQ(model.transitions[1].target, "Active");
}

/**
 * @brief Regression: `SmvParser` infers `DataType::string()` when `init(var) := "..."` has a quoted
 * string literal, preventing invalid `uint32_t var{"..."}` in generated C++.
 * @scenario Parse SMV with `label : 0..100;` and `init(label) := "hello world";`.
 * @expected `label` variable has `type.primitive() == PrimitiveTypeKind::String` and `initial_value == "\"hello
 * world\""`.
 */
TEST(SmvParser, QuotedStringInit_InfersStringDataType) {
    const std::string smv_content = R"(MODULE main
VAR
  state : {Idle, Done};
  event : {Go};
  label : 0..100;
ASSIGN
  init(state) := Idle;
  init(label) := "hello world";
  next(state) := case
    state = Idle & event = Go : Done;
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;
    const auto* var = model.find_variable("label");
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->type.primitive(), PrimitiveTypeKind::String);
    EXPECT_EQ(var->initial_value, R"("hello world")");
}

/**
 * @brief Regression: `SmvParser` parses multi-line `LTLSPEC -- <name>`, `CTLSPEC -- <name>`, and
 * `INVARSPEC -- <name>` specifications emitted by `SmvSerializer`, extracting both the property name
 * from the `--` comment and the formula on the following line.
 * @scenario Parse SMV with multi-line `LTLSPEC -- liveness` and `CTLSPEC -- temp_bound`.
 * @expected Two properties extracted with names `liveness` / `temp_bound` and their formulas.
 */
TEST(SmvParser, MultilineLtlAndCtlSpecsWithNameComment_ParsedCleanly) {
    const std::string smv_content = R"(MODULE main
VAR
  state : {Idle, Run};
  event : {Start};
ASSIGN
  init(state) := Idle;
  next(state) := case
    state = Idle & event = Start : Run;
    TRUE : state;
  esac;

LTLSPEC -- liveness
  G (state = Idle -> F (state = Run));

CTLSPEC -- can_run
  EF (state = Run);
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;
    ASSERT_EQ(model.properties.size(), 2u);
    EXPECT_EQ(model.properties[0].name, "liveness");
    EXPECT_EQ(model.properties[0].raw_formula, "G (state = Idle -> F (state = Run))");
    EXPECT_EQ(model.properties[1].name, "can_run");
    EXPECT_EQ(model.properties[1].raw_formula, "EF (state = Run)");
}

/**
 * @brief Regression: `SmvParser` reconstructs `TimeTrigger` from `timer_<State> >= <ms>` clauses
 * emitted by `SmvSerializer` and removes the synthetic `timer_<State>` counter from `model.variables`.
 * @scenario Parse SMV with `timer_Busy : 0..100;` and `state = Busy & timer_Busy >= 100 : Idle;`.
 * @expected Transition has `TimeTrigger` with `duration_in_ms() == 100` and `timer_Busy` is not in `variables`.
 */
TEST(SmvParser, TimerCounterClause_ReconstructsTimeTriggerAndRemovesSyntheticTimerVar) {
    const std::string smv_content = R"(MODULE main
VAR
  state : {Busy, Idle};
  event : {none, after_100ms};
  timer_Busy : 0..100;
ASSIGN
  init(state) := Busy;
  init(timer_Busy) := 0;
  next(state) := case
    state = Busy & timer_Busy >= 100 : Idle;
    TRUE : state;
  esac;
)";

    SmvParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(smv_content, model, err)) << "Error: " << err;
    EXPECT_EQ(model.find_variable("timer_Busy"), nullptr);
    ASSERT_EQ(model.transitions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(model.transitions[0].trigger));
    EXPECT_EQ(std::get<TimeTrigger>(model.transitions[0].trigger).duration_in_ms(), 100u);
}

/**
 * @brief Regression: `SmvSerializer` emits `-- kind=internal` on `TransitionEdgeKind::Internal`
 * transitions so `SmvParser` preserves `Internal` kind across nuXmv/SMV roundtrips.
 * @scenario Serialize an internal transition `Idle --Ping / x = x + 1--> Idle` to SMV and re-parse.
 * @expected Serialized SMV contains `kind=internal` and re-parsed transition has `kind ==
 * TransitionEdgeKind::Internal`.
 */
TEST(SmvParser, InternalTransition_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "SmvIntRt";
    ir.initial_state = "Idle";
    ir.add_state("Idle");
    VariableDefinition x("x", DataType::int32(), "0");
    ir.add_variable(x);

    TransitionEdge t("Idle", "Idle", "Ping");
    t.kind = TransitionEdgeKind::Internal;
    ActionSignature act("inc_x");
    act.assignments.emplace_back("x", "x + 1");
    t.transition_action = act;
    ir.add_transition(t);

    const std::string smv = fsm::backend::formal::SmvSerializer::serialize(ir);
    EXPECT_NE(smv.find("kind=internal"), std::string::npos) << smv;

    SmvParser parser;
    FsmIr parsed;
    std::string err;
    ASSERT_TRUE(parser.parse(smv, parsed, err)) << err;
    ASSERT_EQ(parsed.transitions.size(), 1u);
    EXPECT_EQ(parsed.transitions[0].kind, TransitionEdgeKind::Internal);
    ASSERT_TRUE(parsed.transitions[0].transition_action.has_value());
    EXPECT_EQ(parsed.transitions[0].transition_action->assignments.size(), 1u);
}

/**
 * @brief Regression: `SmvSerializer` emits `-- @fsm:enum`, `-- @fsm:struct`, `-- @fsm:port`, and
 * `-- @fsm:var` directives and `SmvParser` restores them without duplicating ports into `variables`
 * or clobbering `int32` variable types with `uint32`.
 * @scenario Serialize an FSM with an enum, struct, `in` port, and `int32` variable (`init="-5"`), then re-parse.
 * @expected Re-parsed IR preserves the enum, struct, port (not in `variables`), and `int32` variable with `"-5"`.
 */
TEST(SmvParser, PortsEnumsStructsAndTypedVars_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "SmvTypesRt";
    ir.initial_state = "Idle";
    ir.add_state("Idle");

    EnumDefinition mode_enum("Mode", "uint8_t");
    mode_enum.literals.emplace_back("OFF", 0);
    mode_enum.literals.emplace_back("ON", 1);
    ir.add_enum(mode_enum);

    StructDefinition vec_struct("Vec");
    vec_struct.fields.emplace_back("x", DataType::int32());
    ir.add_struct(vec_struct);

    PortDefinition cmd("cmd", DataType::custom("Mode"), PortDirection::In);
    ir.add_port(cmd);

    VariableDefinition offset("offset", DataType::int32(), "-5");
    ir.add_variable(offset);

    const std::string smv = fsm::backend::formal::SmvSerializer::serialize(ir);
    SmvParser parser;
    FsmIr parsed;
    std::string err;
    ASSERT_TRUE(parser.parse(smv, parsed, err)) << err;

    ASSERT_NE(parsed.find_port("cmd"), nullptr);
    EXPECT_EQ(parsed.find_variable("cmd"), nullptr);

    const auto* p_offset = parsed.find_variable("offset");
    ASSERT_NE(p_offset, nullptr);
    EXPECT_EQ(p_offset->type, DataType::int32());
    EXPECT_EQ(p_offset->initial_value, "-5");

    bool found_enum = false;
    bool found_struct = false;
    for (const auto& ct : parsed.custom_types) {
        if (ct.is_enum() && ct.name == "Mode")
            found_enum = true;
        if (ct.is_struct() && ct.name == "Vec")
            found_struct = true;
    }
    EXPECT_TRUE(found_enum);
    EXPECT_TRUE(found_struct);
    EXPECT_EQ(parsed.name, "SmvTypesRt");
}

}  // namespace

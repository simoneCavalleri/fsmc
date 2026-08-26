#include <gtest/gtest.h>

#include <string>

#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/frontend/diagram/json_parser.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/frontend/formal/scxml_parser.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/frontend/guard_parser.hpp"
#include "fsm/runtime/cpp/fsm.hpp"

namespace {

// Test context
struct SafetyContext {
    bool power_ok = true;
    bool door_closed = true;
    bool emergency_stop = false;
    int temperature = 25;
    bool manual_override = false;
};

// Base atomic guards
struct IsPowerOk {
    bool operator()(const auto& /*evt*/, const auto& /*state*/, const SafetyContext& ctx) const { return ctx.power_ok; }
};

struct IsDoorClosed {
    bool operator()(const auto& /*evt*/, const auto& /*state*/, const SafetyContext& ctx) const {
        return ctx.door_closed;
    }
};

struct IsEmergencyStop {
    bool operator()(const auto& /*evt*/, const auto& /*state*/, const SafetyContext& ctx) const {
        return ctx.emergency_stop;
    }
};

struct IsTempSafe {
    bool operator()(const auto& /*evt*/, const auto& /*state*/, const SafetyContext& ctx) const {
        return ctx.temperature < 80;
    }
};

struct IsManualOverride {
    bool operator()(const auto& /*evt*/, const auto& /*state*/, const SafetyContext& ctx) const {
        return ctx.manual_override;
    }
};

// States & Events
struct Off {
    static constexpr std::string_view name = "Off";
};
struct Running {
    static constexpr std::string_view name = "Running";
};
struct ErrorState {
    static constexpr std::string_view name = "ErrorState";
};

struct StartCmd {};
struct StopCmd {};

/**
 * @brief Test Intent: Verify C++ compile-time composite guard combinators (`and_`, `or_`, `not_`).
 *
 * Scenario:
 * - Evaluate `not_<IsEmergencyStop>`.
 * - Evaluate 3-way conjunction `and_<IsPowerOk, IsDoorClosed, not_<IsEmergencyStop>>`.
 * - Evaluate disjunction `or_<IsEmergencyStop, not_<IsTempSafe>>`.
 * - Evaluate complex nested combinator: `(PowerOk && DoorClosed) || ManualOverride`.
 */
TEST(CompositeGuardsTest, DirectCombinatorsEvaluation) {
    SafetyContext ctx;
    StartCmd evt;
    Off st;

    // not_
    fsm::not_<IsEmergencyStop> not_e_stop;
    EXPECT_TRUE(not_e_stop(evt, st, ctx));
    ctx.emergency_stop = true;
    EXPECT_FALSE(not_e_stop(evt, st, ctx));
    ctx.emergency_stop = false;

    // and_
    using ReadyGuard = fsm::and_<IsPowerOk, IsDoorClosed, fsm::not_<IsEmergencyStop>>;
    ReadyGuard ready_guard;
    EXPECT_TRUE(ready_guard(evt, st, ctx));

    ctx.power_ok = false;
    EXPECT_FALSE(ready_guard(evt, st, ctx));
    ctx.power_ok = true;

    ctx.door_closed = false;
    EXPECT_FALSE(ready_guard(evt, st, ctx));
    ctx.door_closed = true;

    ctx.emergency_stop = true;
    EXPECT_FALSE(ready_guard(evt, st, ctx));
    ctx.emergency_stop = false;

    // or_
    using WarnGuard = fsm::or_<IsEmergencyStop, fsm::not_<IsTempSafe>>;
    WarnGuard warn_guard;
    EXPECT_FALSE(warn_guard(evt, st, ctx));

    ctx.temperature = 95;
    EXPECT_TRUE(warn_guard(evt, st, ctx));
    ctx.temperature = 25;

    ctx.emergency_stop = true;
    EXPECT_TRUE(warn_guard(evt, st, ctx));
    ctx.emergency_stop = false;

    // Complex nested combinator: (PowerOk && DoorClosed) || ManualOverride
    using OverrideGuard = fsm::or_<fsm::and_<IsPowerOk, IsDoorClosed>, IsManualOverride>;
    OverrideGuard override_guard;
    EXPECT_TRUE(override_guard(evt, st, ctx));

    ctx.door_closed = false;
    EXPECT_FALSE(override_guard(evt, st, ctx));

    ctx.manual_override = true;
    EXPECT_TRUE(override_guard(evt, st, ctx));
}

/**
 * @brief Test Intent: Verify AST parsing and operator precedence in GuardExpressionParser.
 *
 * Scenario:
 * - Parse atomic guards, negation `!A`, conjunction `A && B`, and disjunction `A || B`.
 * - Verify `&&` binds tighter than `||` (`A || B && C` -> `fsm::or_<A, fsm::and_<B, C>>`).
 * - Verify parentheses override default precedence (`(A || B) && C` -> `fsm::and_<fsm::or_<A, B>, C>`).
 * - Verify 4-level deep nested boolean formulas.
 */
TEST(CompositeGuardsTest, GuardExpressionParserBasicAndNested) {
    // 1. Single identifier
    {
        auto res = fsm::codegen::GuardExpressionParser::parse("EmergencyStop");
        EXPECT_EQ(res.cpp_type, "EmergencyStop");
        ASSERT_EQ(res.atomic_guards.size(), 1u);
        EXPECT_EQ(res.atomic_guards[0], "EmergencyStop");
    }

    // 2. Unary Not
    {
        auto res = fsm::codegen::GuardExpressionParser::parse("!EmergencyStop");
        EXPECT_EQ(res.cpp_type, "fsm::not_<EmergencyStop>");
        ASSERT_EQ(res.atomic_guards.size(), 1u);
        EXPECT_EQ(res.atomic_guards[0], "EmergencyStop");
    }

    // 3. Binary And
    {
        auto res = fsm::codegen::GuardExpressionParser::parse("PowerOk && !EmergencyStop");
        EXPECT_EQ(res.cpp_type, "fsm::and_<PowerOk, fsm::not_<EmergencyStop>>");
        ASSERT_EQ(res.atomic_guards.size(), 2u);
    }

    // 4. Precedence: && binds tighter than ||
    {
        auto res = fsm::codegen::GuardExpressionParser::parse("A || B && C");
        EXPECT_EQ(res.cpp_type, "fsm::or_<A, fsm::and_<B, C>>");
        ASSERT_EQ(res.atomic_guards.size(), 3u);
    }

    // 5. Parentheses overriding precedence
    {
        auto res = fsm::codegen::GuardExpressionParser::parse("(A || B) && C");
        EXPECT_EQ(res.cpp_type, "fsm::and_<fsm::or_<A, B>, C>");
        ASSERT_EQ(res.atomic_guards.size(), 3u);
    }

    // 6. Deep 4-level nesting
    {
        auto res = fsm::codegen::GuardExpressionParser::parse("((A && !B) || (C && (D || !E)))");
        EXPECT_EQ(res.cpp_type, "fsm::or_<fsm::and_<A, fsm::not_<B>>, fsm::and_<C, fsm::or_<D, fsm::not_<E>>>>");
        ASSERT_EQ(res.atomic_guards.size(), 5u);
    }
}

/**
 * @brief Test Intent: Verify whitespace resilience, empty inputs, and roundtrip diagram string formatting.
 *
 * Scenario:
 * - Parse expressions with irregular whitespace formatting.
 * - Test empty and whitespace-only guard strings.
 * - Test roundtrip conversion between C++ template representation and diagram string format.
 */
TEST(CompositeGuardsTest, GuardExpressionParserEdgeCasesAndFuzzing) {
    // 1. Whitespace resilience
    {
        auto res = fsm::codegen::GuardExpressionParser::parse("  !  EmergencyStop   ");
        EXPECT_EQ(res.cpp_type, "fsm::not_<EmergencyStop>");
    }
    {
        auto res = fsm::codegen::GuardExpressionParser::parse("A&&! B || ( C&&D )");
        EXPECT_EQ(res.cpp_type, "fsm::or_<fsm::and_<A, fsm::not_<B>>, fsm::and_<C, D>>");
    }

    // 2. Empty or whitespace-only expressions
    {
        auto res = fsm::codegen::GuardExpressionParser::parse("");
        EXPECT_TRUE(res.cpp_type.empty());
        EXPECT_TRUE(res.atomic_guards.empty());
    }
    {
        auto res = fsm::codegen::GuardExpressionParser::parse("   \t\n  ");
        EXPECT_TRUE(res.cpp_type.empty());
        EXPECT_TRUE(res.atomic_guards.empty());
    }

    // 3. Roundtrip diagram string formatting
    {
        std::string cpp_t = "fsm::and_<SafetyOk, fsm::not_<EStop>>";
        std::string diagram_s = fsm::codegen::GuardExpressionParser::to_diagram_string(cpp_t);
        EXPECT_EQ(diagram_s, "SafetyOk && !EStop");

        // Re-parsing diagram string gives identical C++ type
        auto reparsed = fsm::codegen::GuardExpressionParser::parse(diagram_s);
        EXPECT_EQ(reparsed.cpp_type, cpp_t);
    }
    {
        std::string cpp_t = "fsm::or_<fsm::and_<A, B>, fsm::not_<C>>";
        std::string diagram_s = fsm::codegen::GuardExpressionParser::to_diagram_string(cpp_t);
        EXPECT_EQ(diagram_s, "A && B || !C");
    }
}

/**
 * @brief Test Intent: Verify composite guard expression extraction across all supported diagram parsers.
 *
 * Scenario:
 * - Parse composite guard expressions from PlantUML, Mermaid, SysML v2, SCXML, DOT, and JSON.
 * - Verify every parser properly decodes entities and compiles the expression into the normalized C++ template type.
 */
TEST(CompositeGuardsTest, MultiFormatParserCompositeGuards) {
    // 1. PlantUML
    {
        fsm::codegen::PlantUmlParser parser;
        fsm::codegen::FsmIr model;
        std::string err;
        std::string puml = R"(
@startuml
[*] --> Off
Off --> Running : StartCmd [PowerOk && !EmergencyStop]
Running --> Off : StopCmd
@enduml
)";
        ASSERT_TRUE(parser.parse(puml, model, err));
        ASSERT_EQ(model.transitions.size(), 2u);
        ASSERT_TRUE(model.transitions[0].guard.has_value());
        EXPECT_EQ(*model.transitions[0].guard, "fsm::and_<PowerOk, fsm::not_<EmergencyStop>>");
        EXPECT_EQ(model.guards.size(), 2u);
    }

    // 2. Mermaid
    {
        fsm::codegen::MermaidParser parser;
        fsm::codegen::FsmIr model;
        std::string err;
        std::string mmd = R"(
stateDiagram-v2
    [*] --> Off
    Off --> Running : StartCmd [PowerOk && DoorClosed]
    Running --> Off : StopCmd
)";
        ASSERT_TRUE(parser.parse(mmd, model, err));
        ASSERT_EQ(model.transitions.size(), 2u);
        EXPECT_EQ(*model.transitions[0].guard, "fsm::and_<PowerOk, DoorClosed>");
    }

    // 3. SysML v2
    {
        fsm::codegen::Sysml2Parser parser;
        fsm::codegen::FsmIr model;
        std::string err;
        std::string sysml = R"(
state def MachineFSM {
    entry; then Off;
    state Off;
    state Running;
    transition from Off accept StartCmd if PowerOk && !EmergencyStop then Running;
}
)";
        ASSERT_TRUE(parser.parse(sysml, model, err));
        ASSERT_EQ(model.transitions.size(), 1u);
        EXPECT_EQ(*model.transitions[0].guard, "fsm::and_<PowerOk, fsm::not_<EmergencyStop>>");
    }

    // 4. SCXML
    {
        fsm::codegen::ScxmlParser parser;
        fsm::codegen::FsmIr model;
        std::string err;
        std::string scxml = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Off">
    <state id="Off">
        <transition event="StartCmd" cond="PowerOk &amp;&amp; !EmergencyStop" target="Running"/>
    </state>
    <state id="Running"/>
</scxml>)";
        ASSERT_TRUE(parser.parse(scxml, model, err));
        ASSERT_EQ(model.transitions.size(), 1u);
        EXPECT_EQ(*model.transitions[0].guard, "fsm::and_<PowerOk, fsm::not_<EmergencyStop>>");
    }

    // 5. DOT
    {
        fsm::codegen::DotParser parser;
        fsm::codegen::FsmIr model;
        std::string err;
        std::string dot = R"(
digraph FSM {
    __start [shape=point];
    __start -> Off;
    Off -> Running [label="StartCmd [PowerOk && !EmergencyStop]"];
}
)";
        ASSERT_TRUE(parser.parse(dot, model, err));
        ASSERT_EQ(model.transitions.size(), 1u);
        EXPECT_EQ(*model.transitions[0].guard, "fsm::and_<PowerOk, fsm::not_<EmergencyStop>>");
    }

    // 6. JSON (XState)
    {
        fsm::codegen::JsonStateParser parser;
        fsm::codegen::FsmIr model;
        std::string err;
        std::string json = R"({
    "id": "MachineFSM",
    "initial": "Off",
    "states": {
        "Off": {
            "on": {
                "StartCmd": { "target": "Running", "guard": "PowerOk && !EmergencyStop" }
            }
        },
        "Running": {}
    }
})";
        ASSERT_TRUE(parser.parse(json, model, err));
        ASSERT_EQ(model.transitions.size(), 1u);
        EXPECT_EQ(*model.transitions[0].guard, "fsm::and_<PowerOk, fsm::not_<EmergencyStop>>");
    }
}

/**
 * @brief Test Intent: Verify end-to-end runtime evaluation of composite guards during event dispatch.
 *
 * Scenario:
 * - Define transition table with `fsm::and_<IsPowerOk, IsDoorClosed, fsm::not_<IsEmergencyStop>>`.
 * - Test failure with power off, door open, and emergency stop active.
 * - Test success when all composite conditions are satisfied, transitioning to Running.
 */
TEST(CompositeGuardsTest, FsmRuntimeExecutionWithCompositeGuards) {
    using Table = fsm::transition_table<
        fsm::row<Off, StartCmd, Running>::when<fsm::and_<IsPowerOk, IsDoorClosed, fsm::not_<IsEmergencyStop>>>,
        fsm::row<Running, StopCmd, Off>>;

    SafetyContext ctx;
    fsm::fsm<Table, SafetyContext, Off> machine(ctx);

    EXPECT_TRUE(machine.is_in_state<Off>());

    // 1. Dispatch StartCmd with power off -> rejected
    ctx.power_ok = false;
    EXPECT_FALSE(machine.dispatch(StartCmd{}));
    EXPECT_TRUE(machine.is_in_state<Off>());

    // 2. Power on, door open -> rejected
    ctx.power_ok = true;
    ctx.door_closed = false;
    EXPECT_FALSE(machine.dispatch(StartCmd{}));
    EXPECT_TRUE(machine.is_in_state<Off>());

    // 3. Door closed, emergency stop active -> rejected
    ctx.door_closed = true;
    ctx.emergency_stop = true;
    EXPECT_FALSE(machine.dispatch(StartCmd{}));
    EXPECT_TRUE(machine.is_in_state<Off>());

    // 4. Emergency stop released -> success!
    ctx.emergency_stop = false;
    EXPECT_TRUE(machine.dispatch(StartCmd{}));
    EXPECT_TRUE(machine.is_in_state<Running>());

    // 5. StopCmd -> Off
    EXPECT_TRUE(machine.dispatch(StopCmd{}));
    EXPECT_TRUE(machine.is_in_state<Off>());
}

}  // namespace

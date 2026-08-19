#include <cassert>
#include <iostream>
#include <string>

#include "codegen/guard_parser.hpp"
#include "codegen/mermaid_parser.hpp"
#include "codegen/plantuml_parser.hpp"
#include "codegen/sysml2_parser.hpp"
#include "codegen/scxml_parser.hpp"
#include "codegen/dot_parser.hpp"
#include "codegen/json_parser.hpp"
#include "codegen/cameo_xmi_parser.hpp"
#include "fsm/fsm.hpp"

namespace {

// Test context
struct SafetyContext {
    bool power_ok = true;
    bool door_closed = true;
    bool emergency_stop = false;
    int temperature = 25;
};

// Base atomic guards
struct IsPowerOk {
    bool operator()(const auto& /*evt*/, const auto& /*state*/, const SafetyContext& ctx) const {
        return ctx.power_ok;
    }
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

// States & Events
struct Off { static constexpr std::string_view name = "Off"; };
struct Running { static constexpr std::string_view name = "Running"; };
struct ErrorState { static constexpr std::string_view name = "ErrorState"; };

struct StartCmd {};
struct StopCmd {};

void test_direct_combinators() {
    std::cout << "[TEST] Direct Combinators (not_, and_, or_)...\n";

    SafetyContext ctx;
    StartCmd evt;
    Off st;

    // not_
    fsm::not_<IsEmergencyStop> not_e_stop;
    assert(not_e_stop(evt, st, ctx) == true);
    ctx.emergency_stop = true;
    assert(not_e_stop(evt, st, ctx) == false);
    ctx.emergency_stop = false;

    // and_
    using ReadyGuard = fsm::and_<IsPowerOk, IsDoorClosed, fsm::not_<IsEmergencyStop>>;
    ReadyGuard ready_guard;
    assert(ready_guard(evt, st, ctx) == true);

    ctx.power_ok = false;
    assert(ready_guard(evt, st, ctx) == false);
    ctx.power_ok = true;

    ctx.door_closed = false;
    assert(ready_guard(evt, st, ctx) == false);
    ctx.door_closed = true;

    ctx.emergency_stop = true;
    assert(ready_guard(evt, st, ctx) == false);
    ctx.emergency_stop = false;

    // or_
    using WarnGuard = fsm::or_<IsEmergencyStop, fsm::not_<IsTempSafe>>;
    WarnGuard warn_guard;
    assert(warn_guard(evt, st, ctx) == false);

    ctx.temperature = 95;
    assert(warn_guard(evt, st, ctx) == true);
    ctx.temperature = 25;

    ctx.emergency_stop = true;
    assert(warn_guard(evt, st, ctx) == true);
    ctx.emergency_stop = false;

    std::cout << "  -> Direct combinators passed.\n";
}

void test_guard_parser() {
    std::cout << "[TEST] Guard Expression Parser AST...\n";

    {
        auto res = fsm::codegen::GuardExpressionParser::parse("!EmergencyStop");
        assert(res.cpp_type == "fsm::not_<EmergencyStop>");
        assert(res.atomic_guards.size() == 1);
        assert(res.atomic_guards[0] == "EmergencyStop");
    }

    {
        auto res = fsm::codegen::GuardExpressionParser::parse("PowerOk && DoorClosed && !EmergencyStop");
        assert(res.cpp_type == "fsm::and_<fsm::and_<PowerOk, DoorClosed>, fsm::not_<EmergencyStop>>");
        assert(res.atomic_guards.size() == 3);
    }

    {
        auto res = fsm::codegen::GuardExpressionParser::parse("EmergencyStop || (!PowerOk && HighTemp)");
        assert(res.cpp_type == "fsm::or_<EmergencyStop, fsm::and_<fsm::not_<PowerOk>, HighTemp>>");
        assert(res.atomic_guards.size() == 3);
    }

    std::cout << "  -> Guard expression parser passed.\n";
}

void test_diagram_parsers_composite_guards() {
    std::cout << "[TEST] Multi-Format Diagram Parsers with Composite Guards...\n";

    // 1. PlantUML
    {
        fsm::codegen::PlantUmlParser parser;
        fsm::codegen::FsmModel model;
        std::string err;
        std::string puml = R"(
@startuml
[*] --> Off
Off --> Running : StartCmd [PowerOk && !EmergencyStop]
Running --> Off : StopCmd
@enduml
)";
        assert(parser.parse(puml, model, err));
        assert(model.transitions.size() == 2);
        assert(model.transitions[0].guard.has_value());
        assert(*model.transitions[0].guard == "fsm::and_<PowerOk, fsm::not_<EmergencyStop>>");
        assert(model.guards.size() == 2);
        auto has_guard_fn = [&](const std::string& name) {
            for (const auto& g : model.guards) {
                if (g.name == name) return true;
            }
            return false;
        };
        assert(has_guard_fn("PowerOk"));
        assert(has_guard_fn("EmergencyStop"));
    }

    // 2. Mermaid
    {
        fsm::codegen::MermaidParser parser;
        fsm::codegen::FsmModel model;
        std::string err;
        std::string mmd = R"(
stateDiagram-v2
    [*] --> Off
    Off --> Running : StartCmd [PowerOk && DoorClosed]
    Running --> Off : StopCmd
)";
        assert(parser.parse(mmd, model, err));
        assert(model.transitions.size() == 2);
        assert(*model.transitions[0].guard == "fsm::and_<PowerOk, DoorClosed>");
    }

    // 3. SysML v2
    {
        fsm::codegen::Sysml2Parser parser;
        fsm::codegen::FsmModel model;
        std::string err;
        std::string sysml = R"(
state def MachineFSM {
    entry; then Off;
    state Off;
    state Running;
    transition from Off accept StartCmd if PowerOk && !EmergencyStop then Running;
}
)";
        assert(parser.parse(sysml, model, err));
        assert(model.transitions.size() == 1);
        assert(*model.transitions[0].guard == "fsm::and_<PowerOk, fsm::not_<EmergencyStop>>");
    }

    // 4. SCXML
    {
        fsm::codegen::ScxmlParser parser;
        fsm::codegen::FsmModel model;
        std::string err;
        std::string scxml = R"(<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Off">
    <state id="Off">
        <transition event="StartCmd" cond="PowerOk &amp;&amp; !EmergencyStop" target="Running"/>
    </state>
    <state id="Running"/>
</scxml>)";
        assert(parser.parse(scxml, model, err));
        assert(model.transitions.size() == 1);
        assert(*model.transitions[0].guard == "fsm::and_<PowerOk, fsm::not_<EmergencyStop>>");
    }

    // 5. DOT
    {
        fsm::codegen::DotParser parser;
        fsm::codegen::FsmModel model;
        std::string err;
        std::string dot = R"(
digraph FSM {
    __start [shape=point];
    __start -> Off;
    Off -> Running [label="StartCmd [PowerOk && !EmergencyStop]"];
}
)";
        assert(parser.parse(dot, model, err));
        assert(model.transitions.size() == 1);
        assert(*model.transitions[0].guard == "fsm::and_<PowerOk, fsm::not_<EmergencyStop>>");
    }

    // 6. JSON (XState)
    {
        fsm::codegen::JsonStateParser parser;
        fsm::codegen::FsmModel model;
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
        assert(parser.parse(json, model, err));
        assert(model.transitions.size() == 1);
        assert(*model.transitions[0].guard == "fsm::and_<PowerOk, fsm::not_<EmergencyStop>>");
    }

    std::cout << "  -> Multi-format parser tests passed.\n";
}

void test_fsm_runtime_with_composite_guards() {
    std::cout << "[TEST] FSM Runtime execution with composite guards...\n";

    using Table = fsm::transition_table<
        fsm::row<Off, StartCmd, Running>::when<fsm::and_<IsPowerOk, IsDoorClosed, fsm::not_<IsEmergencyStop>>>,
        fsm::row<Running, StopCmd, Off>
    >;

    SafetyContext ctx;
    fsm::fsm<Table, SafetyContext, Off> machine(ctx);

    assert(machine.is_in_state<Off>());

    // 1. Dispatch StartCmd with power off -> rejected
    ctx.power_ok = false;
    assert(!machine.dispatch(StartCmd{}));
    assert(machine.is_in_state<Off>());

    // 2. Power on, door open -> rejected
    ctx.power_ok = true;
    ctx.door_closed = false;
    assert(!machine.dispatch(StartCmd{}));
    assert(machine.is_in_state<Off>());

    // 3. Door closed, emergency stop active -> rejected
    ctx.door_closed = true;
    ctx.emergency_stop = true;
    assert(!machine.dispatch(StartCmd{}));
    assert(machine.is_in_state<Off>());

    // 4. Emergency stop released -> success!
    ctx.emergency_stop = false;
    assert(machine.dispatch(StartCmd{}));
    assert(machine.is_in_state<Running>());

    // 5. StopCmd -> Off
    assert(machine.dispatch(StopCmd{}));
    assert(machine.is_in_state<Off>());

    std::cout << "  -> FSM runtime composite guards execution passed.\n";
}

}  // namespace

int main() {
    std::cout << "=== Running Composite Guards Test Suite ===\n";
    test_direct_combinators();
    test_guard_parser();
    test_diagram_parsers_composite_guards();
    test_fsm_runtime_with_composite_guards();
    std::cout << "=== All Composite Guards Tests Passed! ===\n";
    return 0;
}

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "codegen/cpp_generator.hpp"
#include "codegen/fsm_validator.hpp"
#include "codegen/plantuml_parser.hpp"
#include "fsm/fsm.hpp"

using namespace fsm::codegen;

namespace {

struct InternalTracker {
    static std::vector<std::string>& log() {
        static std::vector<std::string> instance;
        return instance;
    }
    static void clear() { log().clear(); }
    static void add(const std::string& msg) { log().emplace_back(msg); }
};

struct ActiveState {
    static void on_enter() { InternalTracker::add("ActiveState::on_enter"); }
    static void on_exit() { InternalTracker::add("ActiveState::on_exit"); }
};

struct PingEvent {};
struct TickEvent {};

struct ResetWatchdogAction {
    void operator()(const PingEvent& /*evt*/, ActiveState& /*src*/, ActiveState& /*dst*/) const {
        InternalTracker::add("Action(ResetWatchdog)");
    }
};

using InternalFsmTable = fsm::transition_table<fsm::internal_row<ActiveState, PingEvent>::then<ResetWatchdogAction>>;

void test_runtime_internal_transition() {
    std::cout << "[TEST] Running test_runtime_internal_transition...\n";
    InternalTracker::clear();

    fsm::fsm<InternalFsmTable> machine;
    assert(machine.is_in_state<ActiveState>());
    assert(InternalTracker::log().size() == 1);
    assert(InternalTracker::log()[0] == "ActiveState::on_enter");

    // Dispatch internal event
    InternalTracker::clear();
    const bool handled = machine.dispatch(PingEvent{});
    assert(handled);
    assert(machine.is_in_state<ActiveState>());

    // ONLY Action should be executed, NO on_exit and NO on_enter!
    assert(InternalTracker::log().size() == 1);
    assert(InternalTracker::log()[0] == "Action(ResetWatchdog)");

    std::cout << "[PASS] test_runtime_internal_transition passed.\n";
}

void test_parser_internal_transition() {
    std::cout << "[TEST] Running test_parser_internal_transition...\n";

    const std::string puml = R"(
    @startuml
    [*] --> Idle
    Idle : Ping / ResetWatchdog
    Idle --> Stopped : StopCmd
    @enduml
    )";

    PlantUmlParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(puml, model, err);

    assert(is_parsed);
    assert(model.transitions.size() == 2);

    bool found_internal = false;
    for (const auto& t : model.transitions) {
        if (t.kind == TransitionKind::Internal) {
            found_internal = true;
            assert(t.source == "Idle");
            assert(t.event == "Ping");
            assert(t.action == "ResetWatchdog");
        }
    }
    assert(found_internal);

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    const std::string code = CppGenerator::generate_header(model, opts);
    assert(code.find("fsm::internal_row<Idle, Ping>::then<ResetWatchdog>") != std::string::npos);

    std::cout << "[PASS] test_parser_internal_transition passed.\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "   RUNNING UML 2.5 INTERNAL TRANS TESTS \n"
              << "========================================\n";

    test_runtime_internal_transition();
    test_parser_internal_transition();

    std::cout << "========================================\n"
              << "   ALL INTERNAL TRANS TESTS PASSED (2/2)\n"
              << "========================================\n";
    return 0;
}

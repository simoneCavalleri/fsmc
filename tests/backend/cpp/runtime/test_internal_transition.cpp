#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"
#include "fsm/backend/cpp/runtime/fsm.hpp"

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

/**
 * @brief Test Intent: Verify internal transitions execute actions without triggering state entry or exit hooks.
 *
 * Scenario:
 * - Enter initial ActiveState (on_enter hook runs).
 * - Dispatch internal transition event (PingEvent).
 * - Verify only the action executes, while on_exit and on_enter hooks are completely bypassed.
 */
TEST(InternalTransitionTest, RuntimeInternalTransitionExecutesActionWithoutEntryExit) {
    InternalTracker::clear();

    fsm::fsm<InternalFsmTable> machine;
    EXPECT_TRUE(machine.is_in_state<ActiveState>());
    ASSERT_EQ(InternalTracker::log().size(), 1u);
    EXPECT_EQ(InternalTracker::log()[0], "ActiveState::on_enter");

    // Dispatch internal event
    InternalTracker::clear();
    const auto handled = machine.dispatch(PingEvent{});
    EXPECT_TRUE(handled.is_success());
    EXPECT_TRUE(machine.is_in_state<ActiveState>());

    // ONLY Action should be executed, NO on_exit and NO on_enter!
    ASSERT_EQ(InternalTracker::log().size(), 1u);
    EXPECT_EQ(InternalTracker::log()[0], "Action(ResetWatchdog)");
}

/**
 * @brief Test Intent: Verify parser recognition of internal transitions and code generation to `fsm::internal_row`.
 *
 * Scenario:
 * - Parse PlantUML syntax `Idle : Ping / ResetWatchdog`.
 * - Verify transition is recorded with TransitionEdgeKind::Internal.
 * - Verify C++ generator outputs `fsm::internal_row<Idle, Ping>::then<ResetWatchdog>`.
 */
TEST(InternalTransitionTest, ParserInternalTransitionAndCodegen) {
    const std::string puml = R"(
    @startuml
    [*] --> Idle
    Idle : Ping / ResetWatchdog
    Idle --> Stopped : StopCmd
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;
    ASSERT_EQ(model.transitions.size(), 2u);

    bool found_internal = false;
    for (const auto& t : model.transitions) {
        if (t.kind == TransitionEdgeKind::Internal) {
            found_internal = true;
            EXPECT_EQ(t.source, "Idle");
            EXPECT_EQ(t.event, "Ping");
            EXPECT_EQ(t.action, "ResetWatchdog");
        }
    }
    EXPECT_TRUE(found_internal);

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    const std::string code = CppGenerator::generate_header(model, opts);
    EXPECT_NE(code.find("fsm::internal_row<Idle, Ping>::then<ResetWatchdog>"), std::string::npos);
}

}  // namespace

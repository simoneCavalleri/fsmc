#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "fsm/backend/cpp/cpp17_standalone_runtime.hpp"
#include "fsm/backend/cpp/cpp20_standalone_runtime.hpp"
#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/cpp/cpp_model_emitter.hpp"
#include "fsm/backend/cpp/cpp_options.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace {

using namespace fsm::codegen;

FsmIr create_sample_ir() {
    FsmIr model;
    model.name = "DeviceController";
    model.ns = "test_ns";
    model.initial_state = "Idle";

    EventModel ev_start{"StartCmd"};
    model.events.push_back(ev_start);

    EventModel ev_stop{"StopCmd"};
    model.events.push_back(ev_stop);

    StateNode st_idle{"Idle"};
    model.states.push_back(st_idle);

    StateNode st_running{"Running"};
    st_running.deferred_events.emplace_back("StartCmd");
    model.states.push_back(st_running);

    GuardModel gd_safe{"IsSafeToStart"};
    model.guards.push_back(gd_safe);

    ActionModel ac_init{"InitializeHardware"};
    model.actions.push_back(ac_init);

    TransitionEdge t1;
    t1.source = "Idle";
    t1.target = "Running";
    t1.event = "StartCmd";
    t1.guard = "IsSafeToStart";
    t1.action = "InitializeHardware";
    model.transitions.push_back(t1);

    TransitionEdge t2;
    t2.source = "Running";
    t2.target = "Idle";
    t2.event = "StopCmd";
    model.transitions.push_back(t2);

    return model;
}

/**
 * @brief Test Intent: Verify granular emission of C++ events, states, guards, actions, and transition tables.
 *
 * Scenario:
 * - Test each CppModelEmitter function independently.
 * - Verify correct type_list for deferred events and row chaining for transition tables.
 */
TEST(CppModularGeneratorTest, CppModelEmitterStandaloneUnits) {
    auto model = create_sample_ir();
    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;

    // 1. Events emission
    std::ostringstream ev_out;
    CppModelEmitter::emit_events(ev_out, model);
    std::string ev_str = ev_out.str();
    EXPECT_NE(ev_str.find("struct StartCmd"), std::string::npos);
    EXPECT_NE(ev_str.find("struct StopCmd"), std::string::npos);

    // 2. States emission (with deferred_events)
    std::ostringstream st_out;
    CppModelEmitter::emit_states(st_out, model);
    std::string st_str = st_out.str();
    EXPECT_NE(st_str.find("struct Idle"), std::string::npos);
    EXPECT_NE(st_str.find("struct Running"), std::string::npos);
    EXPECT_NE(st_str.find("using deferred_events = ::fsm::type_list<StartCmd>;"), std::string::npos);

    // 3. Guards emission
    std::ostringstream gd_out;
    CppModelEmitter::emit_guards(gd_out, model, opts);
    std::string gd_str = gd_out.str();
    EXPECT_NE(gd_str.find("struct IsSafeToStart"), std::string::npos);
    EXPECT_NE(gd_str.find("constexpr bool operator()"), std::string::npos);

    // 4. Actions emission
    std::ostringstream ac_out;
    CppModelEmitter::emit_actions(ac_out, model, opts);
    std::string ac_str = ac_out.str();
    EXPECT_NE(ac_str.find("struct InitializeHardware"), std::string::npos);

    // 5. Table emission
    std::ostringstream tbl_out;
    CppModelEmitter::emit_transition_table(tbl_out, model, opts);
    std::string tbl_str = tbl_out.str();
    EXPECT_NE(tbl_str.find("using DeviceControllerTable = fsm::transition_table<"), std::string::npos);
    EXPECT_NE(tbl_str.find("fsm::row<Idle, StartCmd, Running>::when<IsSafeToStart>::then<InitializeHardware>"),
              std::string::npos);
}

/**
 * @brief Test Intent: Verify C++20 standalone runtime emitter outputs complete runtime classes and traits.
 *
 * Scenario:
 * - Emit Cpp20StandaloneRuntime into an ostringstream.
 * - Verify dispatch_status, transition_info, fsm, and thread_safe_fsm definitions.
 */
TEST(CppModularGeneratorTest, Cpp20StandaloneRuntimeEmitter) {
    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    opts.thread_safe = true;

    std::ostringstream rt_out;
    Cpp20StandaloneRuntime::emit(rt_out, opts);
    std::string rt_str = rt_out.str();

    EXPECT_NE(rt_str.find("enum class dispatch_status : std::uint8_t"), std::string::npos);
    EXPECT_NE(rt_str.find("struct transition_info"), std::string::npos);
    EXPECT_NE(rt_str.find("template <typename Table, typename Context"), std::string::npos);
    EXPECT_NE(rt_str.find("class thread_safe_fsm"), std::string::npos);
}

/**
 * @brief Test Intent: Verify C++17 standalone runtime emitter outputs complete runtime classes and traits.
 *
 * Scenario:
 * - Emit Cpp17StandaloneRuntime into an ostringstream.
 * - Verify C++17 compatible runtime types are generated.
 */
TEST(CppModularGeneratorTest, Cpp17StandaloneRuntimeEmitter) {
    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp17;
    opts.thread_safe = true;

    std::ostringstream rt_out;
    Cpp17StandaloneRuntime::emit(rt_out, opts);
    std::string rt_str = rt_out.str();

    EXPECT_NE(rt_str.find("enum class dispatch_status : std::uint8_t"), std::string::npos);
    EXPECT_NE(rt_str.find("struct transition_info"), std::string::npos);
    EXPECT_NE(rt_str.find("template <typename Table, typename Context"), std::string::npos);
    EXPECT_NE(rt_str.find("class thread_safe_fsm"), std::string::npos);
}

/**
 * @brief Test Intent: Verify unified CppGenerator facade generates complete standalone self-contained header.
 *
 * Scenario:
 * - Generate standalone header for DeviceController FSM.
 * - Verify include guards, namespace wrapping, and synchronous and thread-safe FSM aliases.
 */
TEST(CppModularGeneratorTest, FullFacadeIntegration) {
    auto model = create_sample_ir();
    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    opts.standalone = true;
    opts.thread_safe = true;

    std::string code = CppGenerator::generate_header(model, opts);
    EXPECT_NE(code.find("#pragma once"), std::string::npos);
    EXPECT_NE(code.find("namespace test_ns {"), std::string::npos);
    EXPECT_NE(code.find("using DeviceController = fsm::fsm<DeviceControllerTable"), std::string::npos);
    EXPECT_NE(code.find("using ThreadSafeDeviceController = fsm::thread_safe_fsm<DeviceControllerTable"),
              std::string::npos);
}

}  // namespace

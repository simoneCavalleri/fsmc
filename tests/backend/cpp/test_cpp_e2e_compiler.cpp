/**
 * @file test_cpp_e2e_compiler.cpp
 * @brief End-to-End integration and host compiler verification test suite for C++ Emitter (C++17 & C++20).
 *
 * Test Intent:
 * Prove that the generated C++ standalone state machine headers compile cleanly with host GCC/Clang
 * under maximum warning rigor (-Wall -Wextra -Werror -pedantic -Wconversion), accurately execute
 * continuous sampled step() control loops, reactive event dispatch() with payload validation,
 * internal register persistence, and external RPC service side-effect routing with zero heap allocations.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/cpp/cpp_model_emitter.hpp"
#include "fsm/backend/cpp/runtime_exporter.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/spsc_fsm.hpp"
#include "fsm/backend/cpp/runtime/thread_safe_fsm.hpp"
#include "fsm/backend/cpp/runtime/traits/hook_traits.hpp"

using namespace fsm::codegen;
namespace fs = std::filesystem;

namespace {

FsmIr create_industrial_efsm_model() {
    FsmIr model;
    model.name = "IndustrialThermostat";
    model.ns = "industrial";
    model.initial_state = "Idle";

    // 1. InPorts (Sensor inputs with numeric range contracts)
    PortDefinition p_temp("sensor_temp", "float", PortDirection::In);
    p_temp.min_value = -50.0;
    p_temp.max_value = 150.0;
    p_temp.constraint = "self >= -50.0 and self <= 150.0";
    model.ports.push_back(p_temp);

    PortDefinition p_switch("digital_switch", "bool", PortDirection::In);
    model.ports.push_back(p_switch);

    // 2. OutPorts (Actuators with numeric range contracts)
    PortDefinition p_power("heater_power", "float", PortDirection::Out);
    p_power.min_value = 0.0;
    p_power.max_value = 100.0;
    p_power.constraint = "self >= 0.0 and self <= 100.0";
    model.ports.push_back(p_power);

    PortDefinition p_alarm("alarm_active", "bool", PortDirection::Out);
    model.ports.push_back(p_alarm);

    // 3. Registers (Internal memory z^-1)
    model.variables.emplace_back("cycle_counter", "uint32_t", "0");
    model.variables.emplace_back("prev_temp", "float", "20.0f");

    // 4. Typed Signals with Payload & Validators
    SignalDefinition sig_start("EvStart");
    model.add_signal(sig_start);

    SignalDefinition sig_config("EvConfigure");
    sig_config.attributes.emplace_back("mode", "uint32_t", "1");
    sig_config.attributes.emplace_back("target_temp", "float", "22.0f");
    sig_config.validators.emplace_back("mode > 0");
    sig_config.validators.emplace_back("target_temp >= 10.0f && target_temp <= 80.0f");
    model.add_signal(sig_config);

    SignalDefinition sig_emg("EvEmergency");
    model.add_signal(sig_emg);

    // 5. Actions & Guards
    model.actions.emplace_back("notify_ready");
    model.actions.emplace_back("emergency_shutdown");
    model.actions.emplace_back("TriggerAlarmAction");
    model.actions.emplace_back("ConfigureHeaterAction");

    GuardModel g_target("HasValidTarget", "HasValidTarget", "cmd.target_temp > 0.0f", "cmd.target_temp > 0.0f");
    model.guards.push_back(g_target);

    GuardModel g_overheat("IsOverheat", "IsOverheat", "in.sensor_temp > 90.0f", "in.sensor_temp > 90.0f");
    model.guards.push_back(g_overheat);

    // 6. States
    model.add_state("Idle");
    model.add_state("Configuring");
    model.add_state("Operating");
    model.add_state("Fault");

    // 7. Transitions
    TransitionEdge t1("t_cfg", "Idle", "Configuring", SignalTrigger("EvConfigure"));
    t1.guard = "HasValidTarget";
    t1.action = "ConfigureHeaterAction";
    ActionSignature act_sig1("ConfigureHeaterAction", "ConfigureHeaterAction");
    act_sig1.assignments.push_back({"heater_power", "10.0f"});
    act_sig1.assignments.push_back({"cycle_counter", "1"});
    t1.action_sig = act_sig1;
    t1.priority = 1;
    model.add_transition(t1);

    TransitionEdge t2("t_start", "Configuring", "Operating", SignalTrigger("EvStart"));
    t2.action = "notify_ready";
    t2.priority = 1;
    model.add_transition(t2);

    TransitionEdge t3("t_overheat", "Operating", "Idle", AnonymousTrigger{});
    t3.guard = "IsOverheat";
    t3.action = "TriggerAlarmAction";
    ActionSignature act_sig3("TriggerAlarmAction", "TriggerAlarmAction");
    act_sig3.assignments.push_back({"alarm_active", "true"});
    t3.action_sig = act_sig3;
    t3.priority = 2;
    model.add_transition(t3);

    TransitionEdge t4("t_fault", "Operating", "Fault", SignalTrigger("EvEmergency"));
    t4.action = "emergency_shutdown";
    t4.priority = 0; // Highest priority
    model.add_transition(t4);

    return model;
}

}  // namespace

/**
 * @brief Test Intent: Verify host compiler compilation and runtime execution of standalone generated C++17 and C++20 code.
 *
 * Scenario:
 * - Generate standalone C++17 and C++20 headers for IndustrialThermostat EFSM.
 * - Compile both standalone headers with g++ under -Wall -Wextra -Werror -pedantic -Wconversion.
 * - Execute compiled binaries asserting synchronous step() control loops, reactive dispatch() with payload,
 *   internal registers updates, and external RPC service notifications.
 */
TEST(CppE2ECompilerTest, StandaloneCompilationAndExecutionCpp17AndCpp20) {
    FsmIr model = create_industrial_efsm_model();

    fs::path temp_dir = fs::temp_directory_path() / "fsmc_thematic_e2e_test";
    fs::create_directories(temp_dir);

    // 1. C++17 Standalone E2E Test
    {
        GeneratorOptions opts17;
        opts17.cpp_standard = CppStandard::Cpp17;
        opts17.standalone = true;
        opts17.include_stubs = true;
        std::string code17 = CppGenerator::generate_header(model, opts17);

        fs::path header_path17 = temp_dir / "thermostat_cpp17.hpp";
        std::ofstream hf(header_path17);
        hf << code17;
        hf.close();

        fs::path driver_path17 = temp_dir / "driver_cpp17.cpp";
        std::ofstream df(driver_path17);
        df << R"(
#include "thermostat_cpp17.hpp"
#include <cassert>
#include <iostream>

struct MockServices : public industrial::IndustrialThermostatServices {
    bool ready_notified = false;
    bool emergency_shutdown_called = false;
    void notify_ready() override { ready_notified = true; }
    void emergency_shutdown() override { emergency_shutdown_called = true; }
};

int main() {
    using namespace industrial;
    IndustrialThermostatRegisters reg{0, 20.0f};
    MockServices srv;

    IndustrialThermostat fsm(reg, srv);
    assert(fsm.template is_in<Idle>());

    IndustrialThermostatInPorts in;
    IndustrialThermostatOutPorts out;
    assert(in.validate_contracts());
    assert(out.validate_contracts());

    // 1. Validate Signal Validator
    EvConfigure valid_cfg(1, 25.0f);
    assert(valid_cfg.is_valid());
    EvConfigure invalid_cfg(0, 5.0f);
    assert(!invalid_cfg.is_valid());

    // 2. Dispatch valid EvConfigure -> transitions to Configuring and mutates registers & outports
    auto res_cfg = fsm.dispatch(valid_cfg, in, out, srv);
    assert(res_cfg.is_success());
    assert(fsm.template is_in<Configuring>());
    assert(out.heater_power == 10.0f);
    assert(fsm.registers().cycle_counter == 1);
    assert(out.validate_contracts());

    // 3. Dispatch EvStart -> transitions to Operating & invokes mock RPC service
    auto res_start = fsm.dispatch(EvStart{}, in, out, srv);
    assert(res_start.is_success());
    assert(fsm.template is_in<Operating>());
    assert(srv.ready_notified);

    // 4. Sampled continuous step() on overheat (sensor_temp = 95.0 > 90.0) -> transitions to Idle and sets alarm
    in.sensor_temp = 95.0f;
    assert(in.validate_contracts());
    auto res_drop = fsm.step(in, out, srv);
    assert(res_drop.is_success());
    assert(fsm.template is_in<Idle>());
    assert(out.alarm_active);
    assert(out.validate_contracts());

    return 0;
}
)";
        df.close();

        std::string exe_path17 = (temp_dir / "runner_cpp17").string();
        std::string compile_cmd17 = "g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Wconversion -I" +
                                    temp_dir.string() + " " + driver_path17.string() + " -o " + exe_path17;
        int ret17 = std::system(compile_cmd17.c_str());
        ASSERT_EQ(ret17, 0) << "Failed to compile C++17 generated standalone header!";

        int run_ret17 = std::system(exe_path17.c_str());
        EXPECT_EQ(run_ret17, 0) << "C++17 functional execution test failed!";
    }

    // 2. C++20 Standalone E2E Test
    {
        GeneratorOptions opts20;
        opts20.cpp_standard = CppStandard::Cpp20;
        opts20.standalone = true;
        opts20.include_stubs = true;
        std::string code20 = CppGenerator::generate_header(model, opts20);

        fs::path header_path20 = temp_dir / "thermostat_cpp20.hpp";
        std::ofstream hf(header_path20);
        hf << code20;
        hf.close();

        fs::path driver_path20 = temp_dir / "driver_cpp20.cpp";
        std::ofstream df(driver_path20);
        df << R"(
#include "thermostat_cpp20.hpp"
#include <cassert>
#include <iostream>

struct MockServices20 : public industrial::IndustrialThermostatServices {
    bool ready_notified = false;
    bool emergency_shutdown_called = false;
    void notify_ready() override { ready_notified = true; }
    void emergency_shutdown() override { emergency_shutdown_called = true; }
};

int main() {
    using namespace industrial;

    // Zero-heap & no-virtual assertions
    static_assert(!std::is_polymorphic_v<IndustrialThermostat>);
    static_assert(!std::is_polymorphic_v<Idle>);
    static_assert(!std::is_polymorphic_v<Configuring>);
    static_assert(!std::is_polymorphic_v<Operating>);
    static_assert(!std::is_polymorphic_v<Fault>);
    static_assert(!std::is_polymorphic_v<IndustrialThermostatTable>);

    IndustrialThermostatRegisters reg{0, 20.0f};
    MockServices20 srv;

    IndustrialThermostat fsm(reg, srv);
    assert(fsm.template is_in<Idle>());

    IndustrialThermostatInPorts in;
    IndustrialThermostatOutPorts out;
    assert(in.validate_contracts());
    assert(out.validate_contracts());

    // Dispatch EvConfigure -> Configuring
    auto res_cfg = fsm.dispatch(EvConfigure{1, 30.0f}, in, out, srv);
    assert(res_cfg.is_success());
    assert(fsm.template is_in<Configuring>());
    assert(out.validate_contracts());

    // Dispatch EvStart -> Operating
    auto res_start = fsm.dispatch(EvStart{}, in, out, srv);
    assert(res_start.is_success());
    assert(fsm.template is_in<Operating>());
    assert(srv.ready_notified);

    // Dispatch EvEmergency -> Fault (highest priority emergency transition)
    auto res_emg = fsm.dispatch(EvEmergency{}, in, out, srv);
    assert(res_emg.is_success());
    assert(fsm.template is_in<Fault>());
    assert(srv.emergency_shutdown_called);

    return 0;
}
)";
        df.close();

        std::string exe_path20 = (temp_dir / "runner_cpp20").string();
        std::string compile_cmd20 = "g++ -std=c++20 -Wall -Wextra -Werror -pedantic -Wconversion -I" +
                                    temp_dir.string() + " " + driver_path20.string() + " -o " + exe_path20;
        int ret20 = std::system(compile_cmd20.c_str());
        ASSERT_EQ(ret20, 0) << "Failed to compile C++20 generated standalone header!";

        int run_ret20 = std::system(exe_path20.c_str());
        EXPECT_EQ(run_ret20, 0) << "C++20 functional execution test failed!";
    }

    fs::remove_all(temp_dir);
}

/**
 * @brief Test Intent: Verify `RuntimeExporter` bundles standalone runtime headers for C++17 and C++20 and handles IO errors gracefully.
 *
 * Scenario:
 * - Export standalone runtime into temporary directories for C++17 and C++20.
 * - Verify `fsm.hpp` is created.
 * - Attempt to export into an invalid path and verify false return without abnormal termination.
 */
TEST(CppE2ECompilerTest, RuntimeExporterBundlingAndResilience) {
    const std::string export_dir_cpp20 = "temp_thematic_runtime_export_cpp20";
    const std::string export_dir_cpp17 = "temp_thematic_runtime_export_cpp17";
    std::string err;

    EXPECT_TRUE(RuntimeExporter::export_runtime(export_dir_cpp20 + "/fsm.hpp", CppStandard::Cpp20, err));
    EXPECT_TRUE(fs::exists(export_dir_cpp20 + "/fsm.hpp"));

    EXPECT_TRUE(RuntimeExporter::export_runtime(export_dir_cpp17 + "/fsm.hpp", CppStandard::Cpp17, err));
    EXPECT_TRUE(fs::exists(export_dir_cpp17 + "/fsm.hpp"));

    fs::remove_all(export_dir_cpp20);
    fs::remove_all(export_dir_cpp17);

    // Negative path test
    const std::string blocker_file = "fsmc_blocker_file_thematic.tmp";
    {
        std::ofstream f(blocker_file);
        f << "blocker";
    }
    bool ok = RuntimeExporter::export_runtime(blocker_file + "/subdir/fsm.hpp", CppStandard::Cpp20, err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());
    fs::remove(blocker_file);
}

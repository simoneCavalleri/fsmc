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
#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/spsc_fsm.hpp"
#include "fsm/backend/cpp/runtime/thread_safe_fsm.hpp"
#include "fsm/backend/cpp/runtime/traits/hook_traits.hpp"
#include "fsm/backend/cpp/runtime_exporter.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/action.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/signal_definition.hpp"
#include "fsm/middleend/passes/boundary_action_fusion_pass.hpp"

using namespace fsm::backend::cpp;
using namespace fsm::backend;
using namespace fsm::ir;
using namespace fsm::middleend::passes;
using namespace fsm::diagnostic;
namespace fs = std::filesystem;

namespace {

FsmIr create_industrial_efsm_model() {
    FsmIr model;
    model.name = "IndustrialThermostat";
    model.package = "industrial";

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
    model.actions.emplace_back("enter_operating");
    model.actions.emplace_back("exit_operating");
    model.actions.emplace_back("TriggerAlarmAction");
    model.actions.emplace_back("ConfigureHeaterAction");

    GuardModel g_target("HasValidTarget", "HasValidTarget", "cmd.target_temp > 0.0f", "cmd.target_temp > 0.0f");
    model.guards.push_back(g_target);

    GuardModel g_overheat("IsOverheat", "IsOverheat", "in.sensor_temp > 90.0f", "in.sensor_temp > 90.0f");
    model.guards.push_back(g_overheat);

    // 6. States
    model.add_state("Idle");
    model.add_state("Configuring");
    StateNode operating("Operating");
    operating.entry_actions.emplace_back("enter_operating", "enter_operating");
    operating.exit_actions.emplace_back("exit_operating", "exit_operating");
    model.add_state(operating);
    model.add_state("Fault");

    // 7. Transitions
    TransitionEdge t1("t_cfg", "Idle", "Configuring", SignalTrigger("EvConfigure"));
    t1.guard = "HasValidTarget";
    ActionSignature act_sig1("ConfigureHeaterAction", "ConfigureHeaterAction");
    act_sig1.assignments.push_back({"heater_power", "10.0f"});
    act_sig1.assignments.push_back({"cycle_counter", "1"});
    t1.transition_action = act_sig1;
    t1.priority = 1;
    model.add_transition(t1);

    TransitionEdge t2("t_start", "Configuring", "Operating", SignalTrigger("EvStart"));
    t2.set_action("notify_ready");
    t2.priority = 1;
    model.add_transition(t2);

    TransitionEdge t3("t_overheat", "Operating", "Idle", AnonymousTrigger{});
    t3.guard = "IsOverheat";
    ActionSignature act_sig3("TriggerAlarmAction", "TriggerAlarmAction");
    act_sig3.assignments.push_back({"alarm_active", "true"});
    t3.transition_action = act_sig3;
    t3.priority = 2;
    model.add_transition(t3);

    TransitionEdge t4("t_fault", "Operating", "Fault", SignalTrigger("EvEmergency"));
    t4.set_action("emergency_shutdown");
    t4.priority = 0;  // Highest priority
    model.add_transition(t4);

    return model;
}

}  // namespace

/**
 * @brief Verify end-to-end standalone header generation, compilation under host compiler, and runtime execution.
 * @scenario Generate standalone C++17 and C++20 headers from complex FSM model, compile with host g++, and execute
 * runner binary.
 * @expected Both C++17 and C++20 standalone binaries compile cleanly with -Werror and execute transitions with zero
 * failures.
 */
TEST(CppE2ECompiler, GeneratedStandaloneHeaders_CompileAndExecuteUnderCpp17AndCpp20) {
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
    bool operating_entered = false;
    bool operating_exited = false;
    void notify_ready() override { ready_notified = true; }
    void emergency_shutdown() override { emergency_shutdown_called = true; }
    void enter_operating() override { operating_entered = true; }
    void exit_operating() override { operating_exited = true; }
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
    assert(srv.operating_entered);

    // 4. Sampled continuous step() on overheat (sensor_temp = 95.0 > 90.0) -> transitions to Idle and sets alarm
    in.sensor_temp = 95.0f;
    assert(in.validate_contracts());
    auto res_drop = fsm.step(in, out, srv);
    assert(res_drop.has_transitioned());
    assert(fsm.template is_in<Idle>());
    assert(out.alarm_active);
    assert(srv.operating_exited);
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
    bool operating_entered = false;
    bool operating_exited = false;
    void notify_ready() override { ready_notified = true; }
    void emergency_shutdown() override { emergency_shutdown_called = true; }
    void enter_operating() override { operating_entered = true; }
    void exit_operating() override { operating_exited = true; }
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
    assert(srv.operating_entered);

    // Dispatch EvEmergency -> Fault (highest priority emergency transition)
    auto res_emg = fsm.dispatch(EvEmergency{}, in, out, srv);
    assert(res_emg.is_success());
    assert(fsm.template is_in<Fault>());
    assert(srv.emergency_shutdown_called);
    assert(srv.operating_exited);

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
 * @brief Verify RuntimeExporter bundles standalone runtime headers and resilience code.
 * @scenario Invoke RuntimeExporter to export all embedded runtime headers to temporary directory.
 * @expected All essential runtime headers (fsm.hpp, ring_buffer.hpp, etc.) exist and are non-empty.
 */
TEST(CppE2ECompiler, RuntimeBundling_ExportsHeadersAndResilientCode) {
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

/**
 * @brief Verify BoundaryActionFusionPass produces correctly ordered, non-duplicated lifecycle hooks
 *        in both C++17 and C++20 generated standalone code.
 *
 * @scenario A 3-level hierarchy IR is built:
 *   Root (composite)
 *   ├── CompositeA (composite)  exit_action: "exit_CompA"
 *   │   └── LeafA               exit_action: "exit_LeafA"
 *   └── CompositeB (composite)  entry_action: "enter_CompB"
 *       └── LeafB               entry_action: "enter_LeafB"
 *
 *   An external transition LeafA → LeafB carries a transition action "mid_action".
 *   BoundaryActionFusionPass is run before code generation.
 *
 * @expected The generated driver log contains exactly:
 *   ["exit_LeafA", "exit_CompA", "mid_action", "enter_CompB", "enter_LeafB"]
 *   in that order, with each string appearing exactly once (no double-execution).
 */
TEST(CppE2ECompiler, HierarchicalBoundaryActionFusion_ExecutesExactlyOnceInOrder) {
    // -------------------------------------------------------------------------
    // 1. Build the 3-level hierarchy IR
    // -------------------------------------------------------------------------
    FsmIr model;
    model.name = "HierFsmE2E";
    model.package = "hier";
    model.initial_state = "LeafA";

    // Internal registers tracking exact sequence order and call counts
    model.variables.emplace_back("log_0", "uint32_t", "0");
    model.variables.emplace_back("log_1", "uint32_t", "0");
    model.variables.emplace_back("log_2", "uint32_t", "0");
    model.variables.emplace_back("log_3", "uint32_t", "0");
    model.variables.emplace_back("log_4", "uint32_t", "0");
    model.variables.emplace_back("count_leaf_a", "uint32_t", "0");
    model.variables.emplace_back("count_comp_a", "uint32_t", "0");
    model.variables.emplace_back("count_mid", "uint32_t", "0");
    model.variables.emplace_back("count_comp_b", "uint32_t", "0");
    model.variables.emplace_back("count_leaf_b", "uint32_t", "0");
    model.variables.emplace_back("seq_step", "uint32_t", "0");

    // Transition action registered in model
    model.actions.emplace_back("mid_action");

    // Events
    model.signals.emplace_back("EvJump");

    // States:
    // Root -> CompositeA -> LeafA
    // Root -> CompositeB -> LeafB
    StateNode root("Root");
    root.is_composite = true;

    StateNode compA("CompositeA", "", "Root");
    compA.is_composite = true;
    ActionSignature compA_exit("exit_CompA");
    compA_exit.assignments.push_back({"count_comp_a", "reg.count_comp_a + 1"});
    compA_exit.assignments.push_back({"seq_step", "reg.seq_step + 1"});
    compA_exit.assignments.push_back({"log_1", "reg.seq_step"});
    compA.exit_actions.push_back(compA_exit);

    StateNode leafA("LeafA", "", "CompositeA");
    ActionSignature leafA_exit("exit_LeafA");
    leafA_exit.assignments.push_back({"count_leaf_a", "reg.count_leaf_a + 1"});
    leafA_exit.assignments.push_back({"seq_step", "reg.seq_step + 1"});
    leafA_exit.assignments.push_back({"log_0", "reg.seq_step"});
    leafA.exit_actions.push_back(leafA_exit);

    StateNode compB("CompositeB", "", "Root");
    compB.is_composite = true;
    ActionSignature compB_entry("enter_CompB");
    compB_entry.assignments.push_back({"count_comp_b", "reg.count_comp_b + 1"});
    compB_entry.assignments.push_back({"seq_step", "reg.seq_step + 1"});
    compB_entry.assignments.push_back({"log_3", "reg.seq_step"});
    compB.entry_actions.push_back(compB_entry);

    StateNode leafB("LeafB", "", "CompositeB");
    ActionSignature leafB_entry("enter_LeafB");
    leafB_entry.assignments.push_back({"count_leaf_b", "reg.count_leaf_b + 1"});
    leafB_entry.assignments.push_back({"seq_step", "reg.seq_step + 1"});
    leafB_entry.assignments.push_back({"log_4", "reg.seq_step"});
    leafB.entry_actions.push_back(leafB_entry);

    model.states.push_back(root);
    model.states.push_back(compA);
    model.states.push_back(leafA);
    model.states.push_back(compB);
    model.states.push_back(leafB);

    // Transition LeafA -> LeafB with mid_action
    ActionSignature mid("mid_action");
    mid.assignments.push_back({"count_mid", "reg.count_mid + 1"});
    mid.assignments.push_back({"seq_step", "reg.seq_step + 1"});
    mid.assignments.push_back({"log_2", "reg.seq_step"});
    TransitionEdge cross_edge("LeafA", "LeafB", "EvJump", std::nullopt, mid);
    model.transitions.push_back(cross_edge);

    // -------------------------------------------------------------------------
    // 2. Run BoundaryActionFusionPass
    // -------------------------------------------------------------------------
    DiagnosticEngine diag;
    bool fused = BoundaryActionFusionPass::run(model, diag);
    ASSERT_TRUE(fused) << "BoundaryActionFusionPass must have modified the model";

    // Sanity: after fusion the hooks on the traversed nodes must be cleared.
    EXPECT_TRUE(model.find_state("LeafA")->exit_actions.empty());
    EXPECT_TRUE(model.find_state("CompositeA")->exit_actions.empty());
    EXPECT_TRUE(model.find_state("CompositeB")->entry_actions.empty());
    EXPECT_TRUE(model.find_state("LeafB")->entry_actions.empty());

    // -------------------------------------------------------------------------
    // 3. Generate and compile C++17 and C++20 standalone headers
    // -------------------------------------------------------------------------
    fs::path temp_dir = fs::temp_directory_path() / "fsmc_hier_e2e_boundary_test";
    fs::create_directories(temp_dir);

    // The driver asserts:
    // - strict LCA ordering (log_0=1, log_1=2, log_2=3, log_3=4, log_4=5)
    // - exactly-once execution (each count == 1, no double-execution from cleared hooks)
    // - valid target state arrival (LeafB)
    const std::string driver_source = R"(
#include "hier_fsm.hpp"
#include <cassert>
#include <cstdint>

int main() {
    using namespace hier;

    HierFsmE2E sm;
    assert(sm.template is_in<LeafA>() && "Initial state must be LeafA");

    auto res = sm.dispatch(EvJump{});
    assert(res.is_success() && "Dispatch of EvJump must succeed");
    assert(sm.template is_in<LeafB>() && "Target state must be LeafB");

    const auto& reg = sm.registers();

    // 1. Strict LCA sequence order verification:
    //    Step 1: exit_LeafA
    //    Step 2: exit_CompA
    //    Step 3: mid_action
    //    Step 4: enter_CompB
    //    Step 5: enter_LeafB
    assert(reg.log_0 == 1 && "exit_LeafA must execute 1st");
    assert(reg.log_1 == 2 && "exit_CompA must execute 2nd");
    assert(reg.log_2 == 3 && "mid_action must execute 3rd");
    assert(reg.log_3 == 4 && "enter_CompB must execute 4th");
    assert(reg.log_4 == 5 && "enter_LeafB must execute 5th");

    // 2. Exactly-once execution (cleared lifecycle hooks prevent duplicate invocation):
    assert(reg.count_leaf_a == 1 && "exit_LeafA executed != 1 times");
    assert(reg.count_comp_a == 1 && "exit_CompA executed != 1 times");
    assert(reg.count_mid    == 1 && "mid_action executed != 1 times");
    assert(reg.count_comp_b == 1 && "enter_CompB executed != 1 times");
    assert(reg.count_leaf_b == 1 && "enter_LeafB executed != 1 times");

    assert(reg.seq_step == 5 && "Total sequence steps must equal 5");

    return 0;
}
)";

    auto run_standard_test = [&](const std::string& std_flag, CppStandard cpp_std, const std::string& suffix) {
        GeneratorOptions opts;
        opts.cpp_standard = cpp_std;
        opts.standalone = true;
        opts.include_stubs = true;
        std::string code = CppGenerator::generate_header(model, opts);

        fs::path header_path = temp_dir / ("hier_fsm_" + suffix + ".hpp");
        {
            std::ofstream hf(header_path);
            hf << code;
        }
        // The driver includes "hier_fsm.hpp" so create a canonical symlink/file name.
        fs::path canonical = temp_dir / "hier_fsm.hpp";
        if (fs::exists(canonical))
            fs::remove(canonical);
        fs::copy_file(header_path, canonical);

        fs::path driver_path = temp_dir / ("driver_" + suffix + ".cpp");
        {
            std::ofstream df(driver_path);
            df << driver_source;
        }

        std::string exe_path = (temp_dir / ("runner_" + suffix)).string();
        std::string compile_cmd = "g++ " + std_flag + " -Wall -Wextra -Werror -pedantic -Wconversion -I" +
                                  temp_dir.string() + " " + driver_path.string() + " -o " + exe_path;
        int ret = std::system(compile_cmd.c_str());
        ASSERT_EQ(ret, 0) << "[" << suffix << "] Compilation failed";

        int run_ret = std::system(exe_path.c_str());
        EXPECT_EQ(run_ret, 0) << "[" << suffix << "] Runtime assertion failed — "
                              << "action order or uniqueness violated";
    };

    run_standard_test("-std=c++17", CppStandard::Cpp17, "cpp17");
    run_standard_test("-std=c++20", CppStandard::Cpp20, "cpp20");

    fs::remove_all(temp_dir);
}

/**
 * @brief Test Intent: Verify compilation and execution of flattened orthogonal product states in C++17 and C++20.
 * Scenario: Model with concurrent orthogonal regions is lowered to product states, compiled and executed under host
 * g++. Expected: Synchronous dispatch across orthogonal regions maintains deterministic state isolation and action
 * ordering without deadlock.
 */
TEST(CppE2ECompiler, OrthogonalProductStateExecution_UnderCpp17AndCpp20) {
    FsmIr model;
    model.name = "OrthoE2E";
    model.package = "ortho";
    model.initial_state = "DualChannel";

    model.signals.emplace_back("EvA");
    model.signals.emplace_back("EvB");

    model.add_state("DualChannel", "", StateKind::Parallel);

    OrthogonalRegion r1;
    r1.id = "RegionA";
    r1.name = "RegionA";
    r1.initial_state_id = "A_Idle";
    r1.state_ids = {"A_Idle", "A_Active"};

    OrthogonalRegion r2;
    r2.id = "RegionB";
    r2.name = "RegionB";
    r2.initial_state_id = "B_Idle";
    r2.state_ids = {"B_Idle", "B_Active"};

    model.add_state("A_Idle", "RegionA");
    model.add_state("A_Active", "RegionA");
    model.add_state("B_Idle", "RegionB");
    model.add_state("B_Active", "RegionB");

    auto* par = model.find_state_mut("DualChannel");
    ASSERT_NE(par, nullptr);
    par->orthogonal_regions = {r1, r2};

    model.add_transition(TransitionEdge("A_Idle", "A_Active", "EvA"));
    model.add_transition(TransitionEdge("B_Idle", "B_Active", "EvB"));

    fs::path temp_dir = fs::temp_directory_path() / "fsmc_ortho_e2e_test";
    fs::create_directories(temp_dir);

    const std::string driver_source = R"(
#include "ortho_fsm.hpp"
#include <cassert>
#include <iostream>

struct MockServices : public ortho::OrthoE2EServices {};

int main() {
    using namespace ortho;
    OrthoE2ERegisters reg{};
    MockServices srv;
    OrthoE2EInPorts in;
    OrthoE2EOutPorts out;

    OrthoE2E sm(reg, srv);
    assert(sm.template is_in<DualChannel_A_Idle_B_Idle>());

    // Dispatch EvA: transitions from DualChannel_A_Idle_B_Idle to DualChannel_A_Active_B_Idle
    auto r1 = sm.dispatch(EvA{}, in, out, srv);
    assert(r1.is_success());
    assert(sm.template is_in<DualChannel_A_Active_B_Idle>());

    // Dispatch EvB: transitions from DualChannel_A_Active_B_Idle to DualChannel_A_Active_B_Active
    auto r2 = sm.dispatch(EvB{}, in, out, srv);
    assert(r2.is_success());
    assert(sm.template is_in<DualChannel_A_Active_B_Active>());

    return 0;
}
)";

    auto run_standard_test = [&](const std::string& std_flag, CppStandard cpp_std, const std::string& suffix) {
        GeneratorOptions opts;
        opts.cpp_standard = cpp_std;
        opts.standalone = true;
        opts.include_stubs = true;
        std::string code = CppGenerator::generate_header(model, opts);

        fs::path header_path = temp_dir / ("ortho_fsm_" + suffix + ".hpp");
        {
            std::ofstream hf(header_path);
            hf << code;
        }
        fs::path canonical = temp_dir / "ortho_fsm.hpp";
        if (fs::exists(canonical))
            fs::remove(canonical);
        fs::copy_file(header_path, canonical);

        fs::path driver_path = temp_dir / ("driver_" + suffix + ".cpp");
        {
            std::ofstream df(driver_path);
            df << driver_source;
        }

        std::string exe_path = (temp_dir / ("runner_" + suffix)).string();
        std::string compile_cmd = "g++ " + std_flag + " -Wall -Wextra -Werror -pedantic -Wconversion -I" +
                                  temp_dir.string() + " " + driver_path.string() + " -o " + exe_path;
        int ret = std::system(compile_cmd.c_str());
        ASSERT_EQ(ret, 0) << "[" << suffix << "] Compilation failed";

        int run_ret = std::system(exe_path.c_str());
        EXPECT_EQ(run_ret, 0) << "[" << suffix << "] Runtime execution failed";
    };

    run_standard_test("-std=c++17", CppStandard::Cpp17, "cpp17");
    run_standard_test("-std=c++20", CppStandard::Cpp20, "cpp20");

    fs::remove_all(temp_dir);
}

/**
 * @brief End-to-end verification of TimeTrigger (after_ms) lowering and execution under C++17 and C++20.
 *
 * @scenario State Connecting has a timed transition after(500 ms) to Disconnected.
 * @expected The generated machine:
 *   1. Arms timer for after_ms<500> upon entering Connecting.
 *   2. Advances time with tick(200), remaining in Connecting.
 *   3. Advances time with tick(300), reaching 500 ms threshold, firing transition to Disconnected.
 */
TEST(CppE2ECompiler, GeneratedTimedTransitionExecution_UnderCpp17AndCpp20) {
    FsmIr model;
    model.name = "TimedE2E";
    model.package = "timed";
    model.initial_state = "Connecting";

    model.add_state("Connecting");
    model.add_state("Disconnected");
    model.add_state("Connected");

    model.signals.emplace_back("HandshakeOk");
    model.add_transition(TransitionEdge("Connecting", "Connected", "HandshakeOk"));

    TransitionEdge t_timeout("Connecting", "Disconnected", "");
    t_timeout.trigger = TimeTrigger(TimeTriggerKind::After, 500, TimeUnit::Milliseconds);
    model.add_transition(t_timeout);

    fs::path temp_dir = fs::temp_directory_path() / "fsmc_timed_e2e_test";
    fs::create_directories(temp_dir);

    const std::string driver_source = R"(
#include "timed_fsm.hpp"
#include <cassert>
#include <iostream>

struct MockServices : public timed::TimedE2EServices {};

int main() {
    using namespace timed;
    TimedE2ERegisters reg{};
    MockServices srv;

    TimedE2E sm(reg, srv);
    assert(sm.template is_in<Connecting>());

    // Verify timer is armed automatically upon entering Connecting
    assert(sm.timer_manager().active_count() == 1U);

    // Tick by 200 ms: threshold is 500 ms, so no transition occurs yet
    auto expired1 = sm.tick(200);
    assert(expired1 == 0U);
    assert(sm.template is_in<Connecting>());
    assert(sm.timer_manager().active_count() == 1U);

    // Tick by another 300 ms (total 500 ms): after_ms<500> expires and transitions to Disconnected
    auto expired2 = sm.tick(300);
    assert(expired2 == 1U);
    assert(sm.template is_in<Disconnected>());
    assert(sm.timer_manager().active_count() == 0U);

    return 0;
}
)";

    auto run_standard_test = [&](const std::string& std_flag, CppStandard cpp_std, const std::string& suffix) {
        GeneratorOptions opts;
        opts.cpp_standard = cpp_std;
        opts.standalone = true;
        opts.include_stubs = true;
        std::string code = CppGenerator::generate_header(model, opts);

        fs::path header_path = temp_dir / ("timed_fsm_" + suffix + ".hpp");
        {
            std::ofstream hf(header_path);
            hf << code;
        }
        fs::path canonical = temp_dir / "timed_fsm.hpp";
        if (fs::exists(canonical))
            fs::remove(canonical);
        fs::copy_file(header_path, canonical);

        fs::path driver_path = temp_dir / ("driver_" + suffix + ".cpp");
        {
            std::ofstream df(driver_path);
            df << driver_source;
        }

        std::string exe_path = (temp_dir / ("runner_" + suffix)).string();
        std::string compile_cmd = "g++ " + std_flag + " -Wall -Wextra -Werror -pedantic -Wconversion -I" +
                                  temp_dir.string() + " " + driver_path.string() + " -o " + exe_path;
        int ret = std::system(compile_cmd.c_str());
        ASSERT_EQ(ret, 0) << "[" << suffix << "] Compilation failed";

        int run_ret = std::system(exe_path.c_str());
        EXPECT_EQ(run_ret, 0) << "[" << suffix << "] Runtime execution failed";
    };

    run_standard_test("-std=c++17", CppStandard::Cpp17, "cpp17");
    run_standard_test("-std=c++20", CppStandard::Cpp20, "cpp20");

    fs::remove_all(temp_dir);
}

/**
 * @brief End-to-end verification of state time invariant (permanence bound) enforcement under C++17 and C++20.
 *
 * @scenario State Connecting has permanence invariant stay <= 200ms.
 * @expected The generated machine:
 *   1. Starts in Connecting with invariant satisfied.
 *   2. Advances tick(100), remaining satisfied (100 <= 200).
 *   3. Advances tick(150) (total 250 > 200): triggers invariant violation callback and marks has_invariant_violation().
 *   4. Dispatches HandshakeOk, moving to Connected: clears violation.
 */
TEST(CppE2ECompiler, GeneratedTimeInvariantEnforcement_UnderCpp17AndCpp20) {
    FsmIr model;
    model.name = "InvariantE2E";
    model.package = "safety";
    model.initial_state = "Connecting";

    StateNode st_connecting("Connecting", "Connecting");
    st_connecting.time_invariant = StateTimeInvariant("stay <= 200ms");
    model.add_state(st_connecting);

    model.add_state("Connected");

    model.signals.emplace_back("HandshakeOk");
    model.add_transition(TransitionEdge("Connecting", "Connected", "HandshakeOk"));

    fs::path temp_dir = fs::temp_directory_path() / "fsmc_invariant_e2e_test";
    fs::create_directories(temp_dir);

    const std::string driver_source = R"(
#include "invariant_fsm.hpp"
#include <cassert>
#include <iostream>

struct MockServices : public safety::InvariantE2EServices {};

int main() {
    using namespace safety;
    InvariantE2ERegisters reg{};
    MockServices srv;

    InvariantE2E sm(reg, srv);
    assert(sm.template is_in<Connecting>());
    assert(sm.is_invariant_satisfied());
    assert(!sm.has_invariant_violation());

    std::uint64_t reported_residence = 0;
    std::uint64_t reported_max_stay = 0;
    sm.set_invariant_violation_handler([&](const ::fsm::invariant_violation_info& info) {
        reported_residence = info.residence_time_ms;
        reported_max_stay = info.max_stay_duration_ms;
    });

    // Advance 100 ms: within permanence bound (100 <= 200)
    sm.tick(100);
    assert(sm.state_residence_time() == 100U);
    assert(sm.is_invariant_satisfied());
    assert(!sm.has_invariant_violation());
    assert(reported_residence == 0U);

    // Advance 150 ms: total 250 ms > 200 ms -> invariant violated!
    sm.tick(150);
    assert(sm.state_residence_time() == 250U);
    assert(!sm.is_invariant_satisfied());
    assert(sm.has_invariant_violation());
    assert(reported_residence == 250U);
    assert(reported_max_stay == 200U);
    assert(sm.last_invariant_violation().has_value());
    assert(sm.last_invariant_violation()->state_name == "Connecting");

    // Transition to Connected: clears invariant violation
    auto handled = sm.dispatch(HandshakeOk{});
    assert(handled.is_success());
    assert(sm.template is_in<Connected>());
    assert(sm.is_invariant_satisfied());
    assert(!sm.has_invariant_violation());

    return 0;
}
)";

    auto run_standard_test = [&](const std::string& std_flag, CppStandard cpp_std, const std::string& suffix) {
        GeneratorOptions opts;
        opts.cpp_standard = cpp_std;
        opts.standalone = true;
        opts.include_stubs = true;
        std::string code = CppGenerator::generate_header(model, opts);

        fs::path header_path = temp_dir / ("invariant_fsm_" + suffix + ".hpp");
        {
            std::ofstream hf(header_path);
            hf << code;
        }
        fs::path canonical = temp_dir / "invariant_fsm.hpp";
        if (fs::exists(canonical))
            fs::remove(canonical);
        fs::copy_file(header_path, canonical);

        fs::path driver_path = temp_dir / ("driver_" + suffix + ".cpp");
        {
            std::ofstream df(driver_path);
            df << driver_source;
        }

        std::string exe_path = (temp_dir / ("runner_" + suffix)).string();
        std::string compile_cmd = "g++ " + std_flag + " -Wall -Wextra -Werror -pedantic -Wconversion -I" +
                                  temp_dir.string() + " " + driver_path.string() + " -o " + exe_path;
        int ret = std::system(compile_cmd.c_str());
        ASSERT_EQ(ret, 0) << "[" << suffix << "] Compilation failed";

        int run_ret = std::system(exe_path.c_str());
        EXPECT_EQ(run_ret, 0) << "[" << suffix << "] Runtime execution failed";
    };

    run_standard_test("-std=c++17", CppStandard::Cpp17, "cpp17");
    run_standard_test("-std=c++20", CppStandard::Cpp20, "cpp20");

    fs::remove_all(temp_dir);
}

/**
 * @brief Verify generation and compilation when services interface is non-default-constructible.
 *
 * @scenario The model declares services; the concrete implementation requires explicit constructor arguments.
 * @expected The state machine accepts the injected service reference and executes transitions without
 * requiring default constructibility.
 */
TEST(CppE2ECompiler, NonDefaultConstructibleServices_CompilesAndInjectsProperly_UnderCpp17AndCpp20) {
    FsmIr model;
    model.name = "ServiceE2E";
    model.package = "service_pkg";
    model.initial_state = "Idle";

    model.add_state("Idle");
    model.add_state("Active");

    model.actions.emplace_back("log_telemetry");
    model.signals.emplace_back("Start");
    TransitionEdge t_serv("Idle", "Active", "Start");
    t_serv.set_action("log_telemetry");
    model.add_transition(t_serv);

    fs::path temp_dir = fs::temp_directory_path() / "fsmc_service_e2e_test";
    fs::create_directories(temp_dir);

    const std::string driver_source = R"(
#include "service_fsm.hpp"
#include <cassert>
#include <iostream>

struct ConcreteServices : public service_pkg::ServiceE2EServices {
    int instance_id;
    ConcreteServices() = delete;
    explicit ConcreteServices(int id) : instance_id(id) {}
    void log_telemetry() override {}
};

int main() {
    using namespace service_pkg;
    ServiceE2ERegisters reg{};
    ConcreteServices srv(42);

    ServiceE2E sm(reg, srv);
    assert(sm.template is_in<Idle>());

    auto res = sm.dispatch(Start{});
    assert(res.is_success());
    assert(sm.template is_in<Active>());
    assert(srv.instance_id == 42);

    return 0;
}
)";

    auto run_standard_test = [&](const std::string& std_flag, CppStandard cpp_std, const std::string& suffix) {
        GeneratorOptions opts;
        opts.cpp_standard = cpp_std;
        opts.standalone = true;
        opts.include_stubs = true;
        std::string code = CppGenerator::generate_header(model, opts);

        fs::path header_path = temp_dir / ("service_fsm_" + suffix + ".hpp");
        {
            std::ofstream hf(header_path);
            hf << code;
        }
        fs::path canonical = temp_dir / "service_fsm.hpp";
        if (fs::exists(canonical))
            fs::remove(canonical);
        fs::copy_file(header_path, canonical);

        fs::path driver_path = temp_dir / ("driver_" + suffix + ".cpp");
        {
            std::ofstream df(driver_path);
            df << driver_source;
        }

        std::string exe_path = (temp_dir / ("runner_" + suffix)).string();
        std::string compile_cmd = "g++ " + std_flag + " -Wall -Wextra -Werror -pedantic -Wconversion -I" +
                                  temp_dir.string() + " " + driver_path.string() + " -o " + exe_path;
        int ret = std::system(compile_cmd.c_str());
        ASSERT_EQ(ret, 0) << "[" << suffix << "] Compilation failed";

        int run_ret = std::system(exe_path.c_str());
        EXPECT_EQ(run_ret, 0) << "[" << suffix << "] Runtime execution failed";
    };

    run_standard_test("-std=c++17", CppStandard::Cpp17, "cpp17");
    run_standard_test("-std=c++20", CppStandard::Cpp20, "cpp20");

    fs::remove_all(temp_dir);
}

/**
 * @brief Verify generation with include_stubs=false and thread_safe=false.
 *
 * @scenario The generator options disable stub emission and thread_safe wrappers.
 * @expected 1. Generated code does NOT contain stub structs (user provides them).
 *           2. Generated code does NOT contain ThreadSafe or Spsc aliases.
 *           3. Compiles cleanly and executes under C++17 and C++20.
 */
TEST(CppE2ECompiler, NoStubsAndNoThreadSafeGeneration_CompilesCleanly) {
    FsmIr model;
    model.name = "CustomStubE2E";
    model.package = "custom_pkg";
    model.initial_state = "Off";

    model.add_state("Off");
    model.add_state("On");

    GuardModel gc("CustomGuard", "CustomGuard");
    model.guards.push_back(gc);

    model.actions.emplace_back("CustomAction");

    model.signals.emplace_back("Switch");
    TransitionEdge t("Off", "On", "Switch");
    t.guard = "CustomGuard";
    t.set_action("CustomAction");
    model.add_transition(t);

    fs::path temp_dir = fs::temp_directory_path() / "fsmc_nostub_e2e_test";
    fs::create_directories(temp_dir);

    const std::string driver_source = R"(
#include <cassert>
#include <iostream>

namespace custom_pkg {
struct CustomGuard {
    template <typename... Args>
    bool operator()(Args&&...) const noexcept { return true; }
};

struct CustomAction {
    template <typename... Args>
    void operator()(Args&&...) const noexcept {}
};
} // namespace custom_pkg

#include "custom_fsm.hpp"

struct MockServices : public custom_pkg::CustomStubE2EServices {};

int main() {
    using namespace custom_pkg;
    CustomStubE2ERegisters reg{};
    MockServices srv;

    CustomStubE2E sm(reg, srv);
    assert(sm.template is_in<Off>());

    auto res = sm.dispatch(Switch{});
    assert(res.is_success());
    assert(sm.template is_in<On>());

    return 0;
}
)";

    auto run_standard_test = [&](const std::string& std_flag, CppStandard cpp_std, const std::string& suffix) {
        GeneratorOptions opts;
        opts.cpp_standard = cpp_std;
        opts.standalone = true;
        opts.include_stubs = false;
        opts.thread_safe = false;
        std::string code = CppGenerator::generate_header(model, opts);

        // Verify ThreadSafe and Spsc are NOT emitted
        EXPECT_EQ(code.find("using ThreadSafeCustomStubE2E"), std::string::npos);
        EXPECT_EQ(code.find("using SpscCustomStubE2E"), std::string::npos);

        fs::path header_path = temp_dir / ("custom_fsm_" + suffix + ".hpp");
        {
            std::ofstream hf(header_path);
            hf << code;
        }
        fs::path canonical = temp_dir / "custom_fsm.hpp";
        if (fs::exists(canonical))
            fs::remove(canonical);
        fs::copy_file(header_path, canonical);

        fs::path driver_path = temp_dir / ("driver_" + suffix + ".cpp");
        {
            std::ofstream df(driver_path);
            df << driver_source;
        }

        std::string exe_path = (temp_dir / ("runner_" + suffix)).string();
        std::string compile_cmd = "g++ " + std_flag + " -Wall -Wextra -Werror -pedantic -Wconversion -I" +
                                  temp_dir.string() + " " + driver_path.string() + " -o " + exe_path;
        int ret = std::system(compile_cmd.c_str());
        ASSERT_EQ(ret, 0) << "[" << suffix << "] Compilation failed";

        int run_ret = std::system(exe_path.c_str());
        EXPECT_EQ(run_ret, 0) << "[" << suffix << "] Runtime execution failed";
    };

    run_standard_test("-std=c++17", CppStandard::Cpp17, "cpp17");
    run_standard_test("-std=c++20", CppStandard::Cpp20, "cpp20");

    fs::remove_all(temp_dir);
}

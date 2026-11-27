/**
 * @file test_c_abi_zero_alloc.cpp
 * @brief Tests for pure C99 zero-allocation ABI generation (*_c.h + *_c.cpp) over the unified C++17 FSM backend.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "fsm/backend/cpp/c_abi_generator.hpp"
#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::ir;
using namespace fsm::backend::cpp;

namespace {

std::string find_c_compiler() {
#if defined(_WIN32)
    return "";
#else
    if (std::system("cc --version > /dev/null 2>&1") == 0) {
        return "cc";
    }
    if (std::system("gcc --version > /dev/null 2>&1") == 0) {
        return "gcc";
    }
    if (std::system("clang --version > /dev/null 2>&1") == 0) {
        return "clang";
    }
    return "";
#endif
}

std::string find_cpp_compiler() {
#if defined(_WIN32)
    return "";
#else
    if (std::system("c++ --version > /dev/null 2>&1") == 0) {
        return "c++";
    }
    if (std::system("g++ --version > /dev/null 2>&1") == 0) {
        return "g++";
    }
    if (std::system("clang++ --version > /dev/null 2>&1") == 0) {
        return "clang++";
    }
    return "";
#endif
}

FsmIr build_pump_controller_ir() {
    FsmIr ir;
    ir.name = "PumpController";
    ir.initial_state = "Stopped";
    ir.package = "pump_pkg";

    ir.add_state("Stopped");
    ir.add_state("Running");
    ir.add_state("Fault");

    ir.add_port(PortDefinition("pressure_kpa", "int32_t", PortDirection::In));
    ir.add_port(PortDefinition("valve_open", "bool", PortDirection::Out));
    ir.add_port(PortDefinition("target_rpm", "int32_t", PortDirection::Out));

    ir.add_variable(VariableDefinition("run_count", "int32_t", "0"));

    SignalDefinition start_sig("StartPump");
    start_sig.attributes.emplace_back("requested_rpm", "int32_t", "0");
    ir.add_signal(start_sig);
    ir.add_signal(SignalDefinition("StopPump"));
    ir.add_signal(SignalDefinition("Overpressure"));

    ActionSignature open_act("open_valve");
    open_act.assignments.emplace_back(LValueTarget("valve_open", LValueScope::OutPort), "true");
    open_act.assignments.emplace_back(LValueTarget("target_rpm", LValueScope::OutPort), "ev.requested_rpm");
    open_act.assignments.emplace_back(LValueTarget("run_count", LValueScope::Register), "run_count + 1");

    ActionSignature close_act("close_valve");
    close_act.assignments.emplace_back(LValueTarget("valve_open", LValueScope::OutPort), "false");
    close_act.assignments.emplace_back(LValueTarget("target_rpm", LValueScope::OutPort), "0");

    ir.add_guard("pressure_ok", "Safe inlet pressure", "pressure_kpa < 500", "pressure_kpa < 500");

    TransitionEdge t1("Stopped", "Running", "StartPump");
    t1.set_guard("pressure_ok");
    t1.transition_action = open_act;
    ir.add_transition(t1);

    TransitionEdge t2("Running", "Stopped", "StopPump");
    t2.transition_action = close_act;
    ir.add_transition(t2);

    TransitionEdge t3("Running", "Fault", "Overpressure");
    t3.transition_action = close_act;
    ir.add_transition(t3);

    return ir;
}

}  // namespace

TEST(CAbiZeroAlloc, GeneratesPureC99HeaderAndZeroAllocBridge) {
    FsmIr ir = build_pump_controller_ir();

    GeneratorOptions opts;
    opts.standalone = true;
    opts.thread_safe = false;

    const auto c_bundle = CAbiGenerator::generate(ir, opts, "pump_fsm.hpp", "pump_c.h");

    EXPECT_NE(c_bundle.header.find("extern \"C\""), std::string::npos);
    EXPECT_NE(c_bundle.header.find("pump_controller_fsm_t"), std::string::npos);
    EXPECT_NE(c_bundle.header.find("_align_ld"), std::string::npos);
    EXPECT_NE(c_bundle.header.find("pump_controller_dispatch_start_pump"), std::string::npos);
    EXPECT_EQ(c_bundle.source.find("malloc"), std::string::npos);
    EXPECT_EQ(c_bundle.source.find("free("), std::string::npos);
    EXPECT_NE(c_bundle.source.find("static_assert(sizeof(CppFsm) <= sizeof(pump_controller_fsm_t)"), std::string::npos);
    EXPECT_NE(c_bundle.source.find("static_assert(alignof(CppFsm) <= alignof(pump_controller_fsm_t)"),
              std::string::npos);
}

TEST(CAbiZeroAlloc, E2E_PureC99ClientLinksAndRunsAgainstCpp17Bridge) {
    const std::string cc = find_c_compiler();
    const std::string cxx = find_cpp_compiler();
    if (cc.empty() || cxx.empty()) {
        GTEST_SKIP() << "C/C++ compilers not found in PATH; skipping E2E C99 ABI test.";
    }

    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_c_abi_zero_alloc_test";
    fs::create_directories(tmp_dir);

    FsmIr ir = build_pump_controller_ir();
    GeneratorOptions opts;
    opts.standalone = true;
    opts.thread_safe = false;

    const auto cpp_bundle = CppGenerator::generate_bundle(ir, opts, "pump_ports.hpp");
    const auto c_bundle = CAbiGenerator::generate(ir, opts, "pump_fsm.hpp", "pump_c.h");

    {
        std::ofstream f(tmp_dir / "pump_ports.hpp");
        f << cpp_bundle.ports_header;
    }
    {
        std::ofstream f(tmp_dir / "pump_fsm.hpp");
        f << cpp_bundle.fsm_header;
    }
    {
        std::ofstream f(tmp_dir / "pump_c.h");
        f << c_bundle.header;
    }
    {
        std::ofstream f(tmp_dir / "pump_c.cpp");
        f << c_bundle.source;
    }
    {
        std::ofstream f(tmp_dir / "client.c");
        f << R"c(
#include <assert.h>
#include <string.h>
#include "pump_c.h"

int main(void) {
    pump_controller_fsm_t fsm;
    pump_controller_in_ports_t in_ports;
    pump_controller_out_ports_t out_ports;
    pump_controller_status_t st;

    memset(&in_ports, 0, sizeof(in_ports));
    memset(&out_ports, 0, sizeof(out_ports));

    pump_controller_init(&fsm);
    assert(pump_controller_current_state(&fsm) == PUMP_CONTROLLER_STATE_STOPPED);
    assert(strcmp(pump_controller_state_name(PUMP_CONTROLLER_STATE_STOPPED), "Stopped") == 0);

    /* Guard rejected when pressure >= 500 */
    in_ports.pressure_kpa = 600;
    st = pump_controller_dispatch_start_pump(&fsm, 1800, &in_ports, &out_ports);
    assert(st == PUMP_CONTROLLER_STATUS_GUARD_REJECTED);
    assert(pump_controller_current_state(&fsm) == PUMP_CONTROLLER_STATE_STOPPED);

    /* Guard passes when pressure < 500 */
    in_ports.pressure_kpa = 250;
    st = pump_controller_dispatch_start_pump(&fsm, 1800, &in_ports, &out_ports);
    assert(st == PUMP_CONTROLLER_STATUS_OK);
    assert(pump_controller_current_state(&fsm) == PUMP_CONTROLLER_STATE_RUNNING);
    assert(out_ports.valve_open == true);
    assert(out_ports.target_rpm == 1800);

    /* Stop pump */
    st = pump_controller_dispatch_stop_pump(&fsm, &in_ports, &out_ports);
    assert(st == PUMP_CONTROLLER_STATUS_OK);
    assert(pump_controller_current_state(&fsm) == PUMP_CONTROLLER_STATE_STOPPED);
    assert(out_ports.valve_open == false);
    assert(out_ports.target_rpm == 0);

    /* Null pointer safety */
    assert(pump_controller_dispatch_stop_pump(NULL, &in_ports, &out_ports) == PUMP_CONTROLLER_STATUS_INVALID_ARG);

    /* Spec-to-code SHA-256 attestation */
    assert(strcmp(pump_controller_ir_sha256(), PUMP_CONTROLLER_IR_SHA256) == 0);
    assert(strlen(pump_controller_ir_sha256()) == 64);

    pump_controller_destroy(&fsm);
    return 0;
}
)c";
    }

    const fs::path bridge_obj = tmp_dir / "pump_c.o";
    const fs::path client_obj = tmp_dir / "client.o";
    const fs::path bin = tmp_dir / "c99_client_runner";

    const std::string cxx_cmd = cxx + " -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -I\"" +
                                tmp_dir.string() + "\" -c \"" + (tmp_dir / "pump_c.cpp").string() + "\" -o \"" +
                                bridge_obj.string() + "\"";
    ASSERT_EQ(std::system(cxx_cmd.c_str()), 0) << "C++17 C-ABI bridge failed to compile:\n" << cxx_cmd;

    const std::string cc_cmd = cc + " -std=c99 -Wall -Wextra -Werror -pedantic -I\"" + tmp_dir.string() + "\" -c \"" +
                               (tmp_dir / "client.c").string() + "\" -o \"" + client_obj.string() + "\"";
    ASSERT_EQ(std::system(cc_cmd.c_str()), 0) << "Pure C99 client failed to compile:\n" << cc_cmd;

    const std::string link_cmd = cxx + " -fno-exceptions -fno-rtti \"" + client_obj.string() + "\" \"" +
                                 bridge_obj.string() + "\" -o \"" + bin.string() + "\"";
    ASSERT_EQ(std::system(link_cmd.c_str()), 0) << "Linking C99 client with C++17 bridge failed:\n" << link_cmd;

    ASSERT_EQ(std::system(("\"" + bin.string() + "\"").c_str()), 0) << "C99 ABI E2E runner failed";
}

TEST(CAbiZeroAlloc, E2E_PureC99ServicesCallbackWithUserData) {
    const std::string cc = find_c_compiler();
    const std::string cxx = find_cpp_compiler();
    if (cc.empty() || cxx.empty()) {
        GTEST_SKIP() << "C/C++ compilers not found in PATH; skipping E2E C99 services test.";
    }

    FsmIr ir;
    ir.name = "PidFsm";
    ir.initial_state = "Idle";
    ir.package = "ctrl_pkg";

    ir.add_service(ServiceMethodSignature("compute_pwm", {ServiceParameter("err", DataType("int32_t"))},
                                          DataType("int32_t"), /*pure=*/true, /*noexcept=*/true, "PID callback"));

    ir.add_state("Idle");
    ir.add_state("Active");
    ir.add_port(PortDefinition("error_in", "int32_t", PortDirection::In));
    ir.add_port(PortDefinition("pwm_out", "int32_t", PortDirection::Out));
    ir.add_signal(SignalDefinition("Step"));

    ActionSignature step_act("run_step");
    ActionCallOp call_pid;
    call_pid.function_name = "compute_pwm";
    call_pid.arguments = {"error_in"};
    call_pid.result_target = LValueTarget("pwm_out", LValueScope::OutPort);
    step_act.instructions.emplace_back(std::move(call_pid), "call compute_pwm");

    TransitionEdge tr("Idle", "Active", "Step");
    tr.transition_action = step_act;
    ir.add_transition(std::move(tr));

    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_c_abi_services_test";
    fs::create_directories(tmp_dir);

    GeneratorOptions opts;
    opts.standalone = true;
    opts.thread_safe = false;

    const auto cpp_bundle = CppGenerator::generate_bundle(ir, opts, "pid_ports.hpp");
    const auto c_bundle = CAbiGenerator::generate(ir, opts, "pid_fsm.hpp", "pid_c.h");

    {
        std::ofstream f(tmp_dir / "pid_ports.hpp");
        f << cpp_bundle.ports_header;
    }
    {
        std::ofstream f(tmp_dir / "pid_fsm.hpp");
        f << cpp_bundle.fsm_header;
    }
    {
        std::ofstream f(tmp_dir / "pid_c.h");
        f << c_bundle.header;
    }
    {
        std::ofstream f(tmp_dir / "pid_c.cpp");
        f << c_bundle.source;
    }
    {
        std::ofstream f(tmp_dir / "client_srv.c");
        f << R"c(
#include <assert.h>
#include <string.h>
#include "pid_c.h"

typedef struct {
    int32_t gain;
    int32_t calls;
} c_ctx_t;

static int32_t my_compute_pwm(void* user_data, int32_t err) {
    c_ctx_t* ctx = (c_ctx_t*)user_data;
    ctx->calls += 1;
    return err * ctx->gain;
}

int main(void) {
    c_ctx_t ctx;
    pid_fsm_services_t srv;
    pid_fsm_fsm_t fsm;
    pid_fsm_in_ports_t in_ports;
    pid_fsm_out_ports_t out_ports;

    ctx.gain = 4;
    ctx.calls = 0;
    srv.user_data = &ctx;
    srv.compute_pwm = my_compute_pwm;

    memset(&in_ports, 0, sizeof(in_ports));
    memset(&out_ports, 0, sizeof(out_ports));
    in_ports.error_in = 15;

    pid_fsm_init_with_services(&fsm, &srv);
    assert(pid_fsm_dispatch_step(&fsm, &in_ports, &out_ports) == PID_FSM_STATUS_OK);
    assert(pid_fsm_current_state(&fsm) == PID_FSM_STATE_ACTIVE);
    assert(ctx.calls == 1);
    assert(out_ports.pwm_out == 60);

    pid_fsm_destroy(&fsm);
    return 0;
}
)c";
    }

    const fs::path bridge_obj = tmp_dir / "pid_c.o";
    const fs::path client_obj = tmp_dir / "client_srv.o";
    const fs::path bin = tmp_dir / "c99_srv_runner";

    const std::string cxx_cmd = cxx + " -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -I\"" +
                                tmp_dir.string() + "\" -c \"" + (tmp_dir / "pid_c.cpp").string() + "\" -o \"" +
                                bridge_obj.string() + "\"";
    ASSERT_EQ(std::system(cxx_cmd.c_str()), 0) << "C++17 C-ABI services bridge failed:\n" << cxx_cmd;

    const std::string cc_cmd = cc + " -std=c99 -Wall -Wextra -Werror -pedantic -I\"" + tmp_dir.string() + "\" -c \"" +
                               (tmp_dir / "client_srv.c").string() + "\" -o \"" + client_obj.string() + "\"";
    ASSERT_EQ(std::system(cc_cmd.c_str()), 0) << "Pure C99 services client failed:\n" << cc_cmd;

    const std::string link_cmd = cxx + " -fno-exceptions -fno-rtti \"" + client_obj.string() + "\" \"" +
                                 bridge_obj.string() + "\" -o \"" + bin.string() + "\"";
    ASSERT_EQ(std::system(link_cmd.c_str()), 0) << "Linking C99 services client failed:\n" << link_cmd;

    ASSERT_EQ(std::system(("\"" + bin.string() + "\"").c_str()), 0) << "C99 services runner failed";
}

TEST(CAbiZeroAlloc, E2E_StepTickSnapshotAndLocalPodStruct) {
    const std::string cc = find_c_compiler();
    const std::string cxx = find_cpp_compiler();
    if (cc.empty() || cxx.empty()) {
        GTEST_SKIP() << "C/C++ compilers not found in PATH; skipping E2E C99 parity test.";
    }

    FsmIr ir;
    ir.name = "SensorSupervisor";
    ir.initial_state = "Standby";
    ir.package = "sup_pkg";

    TypeDefinition reading_td("SensorReading", TypeKind::Struct);
    reading_td.fields.emplace_back("temp_c", DataType("int32_t"));
    reading_td.fields.emplace_back("valid", DataType("bool"));
    ir.add_type(std::move(reading_td));

    ir.add_state("Standby");
    ir.add_state("Monitoring");
    ir.add_state("TimedOut");

    ir.add_port(PortDefinition("sensor", "SensorReading", PortDirection::In));
    ir.add_port(PortDefinition("last_temp", "int32_t", PortDirection::Out));
    ir.add_variable(VariableDefinition("samples", "int32_t", "0"));

    // Sampled (anonymous) transition: Standby -> Monitoring when sensor.valid && sensor.temp_c > 20
    ir.add_guard("sensor_ready", "Valid sensor above threshold", "sensor.valid && sensor.temp_c > 20",
                 "sensor.valid && sensor.temp_c > 20");
    ActionSignature latch_act("latch_sample");
    latch_act.assignments.emplace_back(LValueTarget("last_temp", LValueScope::OutPort), "sensor.temp_c");
    latch_act.assignments.emplace_back(LValueTarget("samples", LValueScope::Register), "samples + 1");

    TransitionEdge t_step("Standby", "Monitoring", "");
    t_step.set_guard("sensor_ready");
    t_step.transition_action = latch_act;
    ir.add_transition(std::move(t_step));

    // Timed transition: Monitoring -> TimedOut after 50ms
    TransitionEdge t_timeout("Monitoring", "TimedOut", "");
    t_timeout.trigger = TimeTrigger(50U);
    ir.add_transition(std::move(t_timeout));

    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_c_abi_parity_test";
    fs::create_directories(tmp_dir);

    GeneratorOptions opts;
    opts.standalone = true;
    opts.thread_safe = false;

    const auto cpp_bundle = CppGenerator::generate_bundle(ir, opts, "sup_ports.hpp");
    const auto c_bundle = CAbiGenerator::generate(ir, opts, "sup_fsm.hpp", "sup_c.h");

    {
        std::ofstream f(tmp_dir / "sup_ports.hpp");
        f << cpp_bundle.ports_header;
    }
    {
        std::ofstream f(tmp_dir / "sup_fsm.hpp");
        f << cpp_bundle.fsm_header;
    }
    {
        std::ofstream f(tmp_dir / "sup_c.h");
        f << c_bundle.header;
    }
    {
        std::ofstream f(tmp_dir / "sup_c.cpp");
        f << c_bundle.source;
    }
    {
        std::ofstream f(tmp_dir / "client_parity.c");
        f << R"c(
#include <assert.h>
#include <string.h>
#include "sup_c.h"

int main(void) {
    sensor_supervisor_fsm_t fsm;
    sensor_supervisor_fsm_t restored;
    sensor_supervisor_in_ports_t in_ports;
    sensor_supervisor_out_ports_t out_ports;
    uint8_t snap[256];
    size_t snap_written = 0;
    size_t snap_read = 0;

    memset(&in_ports, 0, sizeof(in_ports));
    memset(&out_ports, 0, sizeof(out_ports));

    sensor_supervisor_init(&fsm);
    assert(sensor_supervisor_current_state(&fsm) == SENSOR_SUPERVISOR_STATE_STANDBY);

    /* Sampled step stays steady when sensor not valid */
    in_ports.sensor.temp_c = 10;
    in_ports.sensor.valid = false;
    assert(sensor_supervisor_step(&fsm, &in_ports, &out_ports) == SENSOR_SUPERVISOR_STATUS_IGNORED);
    assert(sensor_supervisor_current_state(&fsm) == SENSOR_SUPERVISOR_STATE_STANDBY);

    /* Sampled step transitions when sensor valid and temp > 20 */
    in_ports.sensor.temp_c = 42;
    in_ports.sensor.valid = true;
    assert(sensor_supervisor_step(&fsm, &in_ports, &out_ports) == SENSOR_SUPERVISOR_STATUS_OK);
    assert(sensor_supervisor_current_state(&fsm) == SENSOR_SUPERVISOR_STATE_MONITORING);
    assert(out_ports.last_temp == 42);

    /* Advance 30ms (still Monitoring) and take a snapshot */
    assert(sensor_supervisor_tick(&fsm, 30, &in_ports, &out_ports) == SENSOR_SUPERVISOR_STATUS_IGNORED);
    assert(sensor_supervisor_current_state(&fsm) == SENSOR_SUPERVISOR_STATE_MONITORING);

    assert(sensor_supervisor_snapshot_size(&fsm) > 0);
    assert(sensor_supervisor_snapshot_size(&fsm) <= sizeof(snap));
    assert(sensor_supervisor_save_snapshot(&fsm, snap, sizeof(snap), &snap_written));
    assert(snap_written > 0);

    /* Advance original past timeout (30 + 25 = 55ms >= 50ms) */
    assert(sensor_supervisor_tick(&fsm, 25, &in_ports, &out_ports) == SENSOR_SUPERVISOR_STATUS_OK);
    assert(sensor_supervisor_current_state(&fsm) == SENSOR_SUPERVISOR_STATE_TIMED_OUT);

    /* Restore snapshot into fresh FSM: should be back in Monitoring with 20ms remaining */
    sensor_supervisor_init(&restored);
    assert(sensor_supervisor_current_state(&restored) == SENSOR_SUPERVISOR_STATE_STANDBY);
    assert(sensor_supervisor_restore_snapshot(&restored, snap, snap_written, &snap_read));
    assert(snap_read == snap_written);
    assert(sensor_supervisor_current_state(&restored) == SENSOR_SUPERVISOR_STATE_MONITORING);

    /* 20ms more expires the restored timer */
    assert(sensor_supervisor_tick(&restored, 20, &in_ports, &out_ports) == SENSOR_SUPERVISOR_STATUS_OK);
    assert(sensor_supervisor_current_state(&restored) == SENSOR_SUPERVISOR_STATE_TIMED_OUT);

    sensor_supervisor_destroy(&restored);
    sensor_supervisor_destroy(&fsm);
    return 0;
}
)c";
    }

    const fs::path bridge_obj = tmp_dir / "sup_c.o";
    const fs::path client_obj = tmp_dir / "client_parity.o";
    const fs::path bin = tmp_dir / "c99_parity_runner";

    const std::string cxx_cmd = cxx + " -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -I\"" +
                                tmp_dir.string() + "\" -c \"" + (tmp_dir / "sup_c.cpp").string() + "\" -o \"" +
                                bridge_obj.string() + "\"";
    ASSERT_EQ(std::system(cxx_cmd.c_str()), 0) << "C++17 parity bridge failed:\n" << cxx_cmd;

    const std::string cc_cmd = cc + " -std=c99 -Wall -Wextra -Werror -pedantic -I\"" + tmp_dir.string() + "\" -c \"" +
                               (tmp_dir / "client_parity.c").string() + "\" -o \"" + client_obj.string() + "\"";
    ASSERT_EQ(std::system(cc_cmd.c_str()), 0) << "Pure C99 parity client failed:\n" << cc_cmd;

    const std::string link_cmd = cxx + " -fno-exceptions -fno-rtti \"" + client_obj.string() + "\" \"" +
                                 bridge_obj.string() + "\" -o \"" + bin.string() + "\"";
    ASSERT_EQ(std::system(link_cmd.c_str()), 0) << "Linking C99 parity client failed:\n" << link_cmd;

    ASSERT_EQ(std::system(("\"" + bin.string() + "\"").c_str()), 0) << "C99 parity runner failed";
}

/**
 * @brief Regression (BUG-63): Verify C-ABI `<prefix>_current_state()` reports the exact active leaf substate
 * inside a composite state rather than masking it behind the composite parent via `is_in<Parent>()`.
 */
TEST(CAbiZeroAlloc, CompositeHierarchy_CurrentStateReportsExactActiveLeafSubstate) {
    FsmIr ir;
    ir.name = "CompAbiFsm";
    ir.initial_state = "Outer";
    ir.package = "comp_pkg";

    auto& outer = ir.add_state("Outer");
    outer.is_composite = true;
    outer.initial_sub_state = "Sub1";
    ir.add_state("Sub1", "Outer");
    ir.add_state("Sub2", "Outer");
    ir.add_state("Escaped");
    ir.add_signal(SignalDefinition("EvNext"));
    ir.add_signal(SignalDefinition("EvEscape"));
    ir.add_transition(TransitionEdge("Sub1", "Sub2", "EvNext"));
    ir.add_transition(TransitionEdge("Outer", "Escaped", "EvEscape"));

    GeneratorOptions opts;
    opts.standalone = true;
    const auto c_bundle = CAbiGenerator::generate(ir, opts, "comp_fsm.hpp", "comp_c.h");

    EXPECT_NE(c_bundle.source.find("sm->is_in_state<::comp_pkg::Outer>()"), std::string::npos);
    EXPECT_NE(c_bundle.source.find("sm->is_in_state<::comp_pkg::Sub1>()"), std::string::npos);
    EXPECT_EQ(c_bundle.source.find("sm->is_in<::comp_pkg::Outer>()"), std::string::npos);
}

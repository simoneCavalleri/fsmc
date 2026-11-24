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

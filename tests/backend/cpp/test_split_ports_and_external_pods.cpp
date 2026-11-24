/**
 * @file test_split_ports_and_external_pods.cpp
 * @brief Tests for external POD/enum type bindings (`external_binding`) and default split header emission
 * (`*_ports.hpp` + `*_fsm.hpp`) with zero fsm.hpp dependency in the ports header.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_deserializer.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"

using namespace fsm::ir;
using namespace fsm::backend::cpp;

namespace {

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

FsmIr build_charger_ir() {
    FsmIr ir;
    ir.name = "ChargerFsm";
    ir.initial_state = "Idle";
    ir.package = "charger_pkg";

    ir.add_type(TypeDefinition::make_external("BatteryStatus", "shared_types.hpp", "::shared::BatteryStatus",
                                              TypeKind::Struct, "Shared BMS telemetry POD"));
    ir.add_type(TypeDefinition::make_external("PowerMode", "shared_types.hpp", "::shared::PowerMode", TypeKind::Enum,
                                              "Shared power mode enum"));

    ir.add_state("Idle");
    ir.add_state("Charging");

    ir.add_port(PortDefinition("cmd_mode", "PowerMode", PortDirection::In));
    ir.add_port(PortDefinition("batt_out", "BatteryStatus", PortDirection::Out));

    ir.add_signal(SignalDefinition("PlugIn"));
    ir.add_signal(SignalDefinition("Unplug"));

    ir.add_transition(TransitionEdge("Idle", "Charging", "PlugIn"));
    ir.add_transition(TransitionEdge("Charging", "Idle", "Unplug"));
    return ir;
}

FsmIr build_drive_ir() {
    FsmIr ir;
    ir.name = "DriveFsm";
    ir.initial_state = "Standby";
    ir.package = "drive_pkg";

    ir.add_type(TypeDefinition::make_external("BatteryStatus", "shared_types.hpp", "::shared::BatteryStatus",
                                              TypeKind::Struct, "Shared BMS telemetry POD"));

    ir.add_state("Standby");
    ir.add_state("Driving");

    ir.add_port(PortDefinition("batt_in", "BatteryStatus", PortDirection::In));
    ir.add_port(PortDefinition("torque_cmd", "int32_t", PortDirection::Out));

    ir.add_signal(SignalDefinition("EnableDrive"));
    ir.add_signal(SignalDefinition("StopDrive"));

    ir.add_transition(TransitionEdge("Standby", "Driving", "EnableDrive"));
    ir.add_transition(TransitionEdge("Driving", "Standby", "StopDrive"));
    return ir;
}

}  // namespace

TEST(SplitPortsAndExternalPods, ExternalTypeBinding_IrJsonRoundTrip_IsLossless) {
    FsmIr original = build_charger_ir();
    original.canonicalize();

    const std::string json1 = FsmIrSerializer::serialize_json(original);
    EXPECT_NE(json1.find("\"external_header\": \"shared_types.hpp\""), std::string::npos);
    EXPECT_NE(json1.find("\"external_qualified_name\": \"::shared::BatteryStatus\""), std::string::npos);
    EXPECT_NE(json1.find("\"external_qualified_name\": \"::shared::PowerMode\""), std::string::npos);

    FsmIr parsed;
    std::string err;
    FsmIrDeserializer::DeserializeOptions opts;
    opts.strict_ir_json = true;
    ASSERT_TRUE(FsmIrDeserializer::deserialize_json(json1, parsed, err, opts)) << err;
    parsed.canonicalize();

    const auto* batt_td = parsed.find_type("BatteryStatus");
    ASSERT_NE(batt_td, nullptr);
    ASSERT_TRUE(batt_td->is_external());
    EXPECT_EQ(batt_td->external_binding->header, "shared_types.hpp");
    EXPECT_EQ(batt_td->external_binding->qualified_name, "::shared::BatteryStatus");

    const auto* mode_td = parsed.find_type("PowerMode");
    ASSERT_NE(mode_td, nullptr);
    ASSERT_TRUE(mode_td->is_external());
    EXPECT_EQ(mode_td->external_binding->header, "shared_types.hpp");
    EXPECT_EQ(mode_td->external_binding->qualified_name, "::shared::PowerMode");

    const std::string json2 = FsmIrSerializer::serialize_json(parsed);
    EXPECT_EQ(json1, json2);
}

TEST(SplitPortsAndExternalPods, SplitBundle_PortsHeaderHasZeroFsmRuntimeDependency) {
    FsmIr model = build_charger_ir();

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp17;
    opts.standalone = true;
    opts.thread_safe = false;

    const GeneratedCppBundle bundle = CppGenerator::generate_bundle(model, opts, "charger_ports.hpp");

    // Ports header: includes external header, emits using aliases (not struct redefinition), zero ::fsm:: dependency
    EXPECT_NE(bundle.ports_header.find("#include \"shared_types.hpp\""), std::string::npos);
    EXPECT_NE(bundle.ports_header.find("using BatteryStatus = ::shared::BatteryStatus;"), std::string::npos);
    EXPECT_NE(bundle.ports_header.find("using PowerMode = ::shared::PowerMode;"), std::string::npos);
    EXPECT_EQ(bundle.ports_header.find("struct BatteryStatus {"), std::string::npos);
    EXPECT_EQ(bundle.ports_header.find("enum class PowerMode"), std::string::npos);
    EXPECT_NE(bundle.ports_header.find("struct ChargerFsmInPorts {"), std::string::npos);
    EXPECT_NE(bundle.ports_header.find("struct ChargerFsmOutPorts {"), std::string::npos);
    EXPECT_NE(bundle.ports_header.find("struct ChargerFsmServices {};"), std::string::npos);
    EXPECT_NE(bundle.ports_header.find("struct PlugIn {"), std::string::npos);
    EXPECT_EQ(bundle.ports_header.find("::fsm::"), std::string::npos);
    EXPECT_EQ(bundle.ports_header.find("#include \"fsm.hpp\""), std::string::npos);
    EXPECT_EQ(bundle.ports_header.find("#include <fsm.hpp>"), std::string::npos);
    EXPECT_EQ(bundle.ports_header.find("struct Idle {"), std::string::npos);

    // FSM header: includes ports header and contains the state machine engine
    EXPECT_NE(bundle.fsm_header.find("#include \"charger_ports.hpp\""), std::string::npos);
    EXPECT_NE(bundle.fsm_header.find("struct Idle {"), std::string::npos);
    EXPECT_NE(bundle.fsm_header.find("::fsm::transition_table<"), std::string::npos);
}

TEST(SplitPortsAndExternalPods, E2E_HalIncludesOnlyPortsAndMultiFsmSharesExternalPod) {
    const std::string cxx = find_cpp_compiler();
    if (cxx.empty()) {
        GTEST_SKIP() << "No C++ compiler found in PATH; skipping E2E split/external POD compile test.";
    }

    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_split_external_pods_test";
    fs::create_directories(tmp_dir);

    // 1. Shared external POD header
    {
        std::ofstream f(tmp_dir / "shared_types.hpp");
        f << R"cpp(
#pragma once
#include <cstdint>

            namespace shared {

            enum class PowerMode : uint8_t { Off = 0, FastCharge = 1 };

            struct BatteryStatus {
                int32_t soc_pct{0};
                bool healthy{true};
            };

            }  // namespace shared
        )cpp";
    }

    // 2. Emit split bundles for ChargerFsm and DriveFsm
    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp17;
    opts.standalone = true;
    opts.thread_safe = false;

    const auto charger_bundle = CppGenerator::generate_bundle(build_charger_ir(), opts, "charger_ports.hpp");
    const auto drive_bundle = CppGenerator::generate_bundle(build_drive_ir(), opts, "drive_ports.hpp");

    {
        std::ofstream f(tmp_dir / "charger_ports.hpp");
        f << charger_bundle.ports_header;
    }
    {
        std::ofstream f(tmp_dir / "charger_fsm.hpp");
        f << charger_bundle.fsm_header;
    }
    {
        std::ofstream f(tmp_dir / "drive_ports.hpp");
        f << drive_bundle.ports_header;
    }
    {
        std::ofstream f(tmp_dir / "drive_fsm.hpp");
        f << drive_bundle.fsm_header;
    }

    // 3. HAL driver TU including ONLY charger_ports.hpp (zero fsm.hpp / zero FSM engine)
    {
        std::ofstream f(tmp_dir / "hal_driver.cpp");
        f << R"cpp(
#include "charger_ports.hpp"

            void hal_sample_battery(charger_pkg::ChargerFsmOutPorts& out) {
                out.batt_out.soc_pct = 88;
                out.batt_out.healthy = true;
            }
        )cpp";
    }

    // 4. Multi-FSM orchestration main.cpp sharing ::shared::BatteryStatus with zero conversion
    {
        std::ofstream f(tmp_dir / "main.cpp");
        f << R"cpp(
#include <cassert>
#include <type_traits>
#include "charger_fsm.hpp"
#include "drive_fsm.hpp"

            static_assert(std::is_same_v<charger_pkg::BatteryStatus, ::shared::BatteryStatus>);
            static_assert(std::is_same_v<drive_pkg::BatteryStatus, ::shared::BatteryStatus>);
            static_assert(std::is_same_v<charger_pkg::BatteryStatus, drive_pkg::BatteryStatus>);

            void hal_sample_battery(charger_pkg::ChargerFsmOutPorts& out);

            int main() {
                charger_pkg::ChargerFsm charger;
                charger_pkg::ChargerFsmInPorts c_in{};
                c_in.cmd_mode = shared::PowerMode::FastCharge;
                charger_pkg::ChargerFsmOutPorts c_out{};

                hal_sample_battery(c_out);
                auto r1 = charger.dispatch(charger_pkg::PlugIn{}, c_in, c_out);
                assert(r1.is_success());
                assert(charger.is_in<charger_pkg::Charging>());

                // Direct zero-copy handoff of shared external POD between independent FSMs
                drive_pkg::DriveFsm drive;
                drive_pkg::DriveFsmInPorts d_in{};
                d_in.batt_in = c_out.batt_out;
                drive_pkg::DriveFsmOutPorts d_out{};

                auto r2 = drive.dispatch(drive_pkg::EnableDrive{}, d_in, d_out);
                assert(r2.is_success());
                assert(drive.is_in<drive_pkg::Driving>());
                assert(d_in.batt_in.soc_pct == 88);
                assert(d_in.batt_in.healthy);

                return 0;
            }
        )cpp";
    }

    // Compile HAL TU independently (must succeed with -fno-exceptions -fno-rtti -Wall -Wextra -Werror)
    const fs::path hal_obj = tmp_dir / "hal_driver.o";
    const std::string hal_cmd = cxx + " -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -I\"" +
                                tmp_dir.string() + "\" -c \"" + (tmp_dir / "hal_driver.cpp").string() + "\" -o \"" +
                                hal_obj.string() + "\"";
    ASSERT_EQ(std::system(hal_cmd.c_str()), 0) << "HAL TU failed to compile with only *_ports.hpp:\n" << hal_cmd;

    // Compile and link main + HAL TU
    const fs::path bin = tmp_dir / "multi_fsm_runner";
    const std::string link_cmd = cxx + " -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -I\"" +
                                 tmp_dir.string() + "\" \"" + (tmp_dir / "main.cpp").string() + "\" \"" +
                                 hal_obj.string() + "\" -o \"" + bin.string() + "\"";
    ASSERT_EQ(std::system(link_cmd.c_str()), 0) << "Multi-FSM E2E link failed:\n" << link_cmd;

    ASSERT_EQ(std::system(("\"" + bin.string() + "\"").c_str()), 0) << "Multi-FSM E2E binary returned non-zero";
}

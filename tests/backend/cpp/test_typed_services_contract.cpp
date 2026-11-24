/**
 * @file test_typed_services_contract.cpp
 * @brief Unit and E2E compile-time tests for explicitly typed Services (sys-math / HAL) and ActionCallOp.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/common/ir_json_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/middleend/passes/verify_ir_integrity_pass.hpp"

namespace fs = std::filesystem;

namespace {

using namespace fsm::ir;
using namespace fsm::backend::cpp;
using namespace fsm::diagnostic;
using namespace fsm::frontend;
using namespace fsm::middleend::passes;

FsmIr make_pid_controller_ir() {
    FsmIr ir;
    ir.name = "MotorPidFsm";
    ir.initial_state = "Idle";

    ir.add_port(PortDefinition("setpoint", "float", PortDirection::In));
    ir.add_port(PortDefinition("measured", "float", PortDirection::In));
    ir.add_port(PortDefinition("pwm_out", "float", PortDirection::Out));
    ir.add_variable(VariableDefinition("last_duty", DataType::float32(), "0.0f"));

    ir.add_service(ServiceMethodSignature(
        "pid_step",
        {ServiceParameter("setpoint", DataType::float32()), ServiceParameter("measured", DataType::float32())},
        DataType::float32(), /*pure=*/true, /*noexcept=*/true, "Pure sys-math PID step"));
    ir.add_service(ServiceMethodSignature("hal_apply_pwm", {ServiceParameter("duty", DataType::float32())},
                                          DataType(PrimitiveTypeKind::Void), /*pure=*/false, /*noexcept=*/true,
                                          "HAL PWM driver write"));

    ir.add_state("Idle");
    ir.add_state("Controlling");
    ir.add_event("Step");

    TransitionEdge tr("Idle", "Controlling", "Step");
    ActionSignature act("run_pid");
    ActionCallOp call_pid;
    call_pid.function_name = "pid_step";
    call_pid.arguments = {"setpoint", "measured"};
    call_pid.result_target = LValueTarget("last_duty", LValueScope::Register);
    act.instructions.emplace_back(std::move(call_pid), "compute PID");

    ActionCallOp call_hal;
    call_hal.function_name = "hal_apply_pwm";
    call_hal.arguments = {"last_duty"};
    act.instructions.emplace_back(std::move(call_hal), "write HAL PWM");

    tr.transition_action = act;
    ir.transitions.push_back(std::move(tr));
    ir.canonicalize();
    return ir;
}

/**
 * @brief Verify lossless ir-json round-trip of ServiceMethodSignature and ActionCallOp::result_target.
 * @scenario Serialize an IR with typed services and ActionCallOp::result_target, re-ingest via IrJsonParser.
 * @expected Round-trip JSON is byte-identical and passes VerifyIrIntegrityPass.
 */
TEST(TypedServicesContractTest, IrJsonRoundtripAndIntegrityValidation) {
    FsmIr ir = make_pid_controller_ir();
    const std::string json1 = FsmIrSerializer::serialize_json(ir);
    EXPECT_NE(json1.find("\"services\""), std::string::npos);
    EXPECT_NE(json1.find("\"pid_step\""), std::string::npos);
    EXPECT_NE(json1.find("\"result_target\": \"last_duty\""), std::string::npos);

    FsmIr parsed;
    DiagnosticEngine diag;
    IrJsonParser parser;
    ASSERT_TRUE(parser.parse(json1, parsed, diag)) << diag.render_to_string();
    parsed.canonicalize();

    EXPECT_EQ(FsmIrSerializer::serialize_json(parsed), json1);

    VerifyIrIntegrityPass integrity;
    DiagnosticEngine int_diag;
    EXPECT_TRUE(integrity.run(parsed, int_diag)) << int_diag.render_to_string();
}

/**
 * @brief Verify VerifyIrIntegrityPass catches undeclared services, wrong arity, void result assignment, and InPort
 * write.
 * @scenario Mutate ActionCallOp calls in a typed-service IR and run VerifyIrIntegrityPass.
 * @expected All 4 invalid cases fail with E_IR_TYPE_MISMATCH.
 */
TEST(TypedServicesContractTest, VerifyIrIntegrity_RejectsInvalidServiceCalls) {
    VerifyIrIntegrityPass pass;

    auto has_code = [](const DiagnosticEngine& d, std::string_view code) {
        for (const auto& item : d.get_diagnostics()) {
            if (item.code == code) {
                return true;
            }
        }
        return false;
    };

    // 1. Wrong arity
    {
        FsmIr ir = make_pid_controller_ir();
        auto& call = std::get<ActionCallOp>(ir.transitions[0].transition_action->instructions[0].op);
        call.arguments.pop_back();
        DiagnosticEngine diag;
        EXPECT_FALSE(pass.run(ir, diag));
        EXPECT_TRUE(has_code(diag, "E_IR_TYPE_MISMATCH"));
    }

    // 2. Assigning result of void service
    {
        FsmIr ir = make_pid_controller_ir();
        auto& call = std::get<ActionCallOp>(ir.transitions[0].transition_action->instructions[1].op);
        call.result_target = LValueTarget("last_duty", LValueScope::Register);
        DiagnosticEngine diag;
        EXPECT_FALSE(pass.run(ir, diag));
        EXPECT_TRUE(has_code(diag, "E_IR_TYPE_MISMATCH"));
    }

    // 3. Storing service result into read-only InPort
    {
        FsmIr ir = make_pid_controller_ir();
        auto& call = std::get<ActionCallOp>(ir.transitions[0].transition_action->instructions[0].op);
        call.result_target = LValueTarget("setpoint", LValueScope::OutPort);
        DiagnosticEngine diag;
        EXPECT_FALSE(pass.run(ir, diag));
        EXPECT_TRUE(has_code(diag, "E_IR_TYPE_MISMATCH"));
    }

    // 4. Undeclared service call when services are declared
    {
        FsmIr ir = make_pid_controller_ir();
        auto& call = std::get<ActionCallOp>(ir.transitions[0].transition_action->instructions[0].op);
        call.function_name = "nonexistent_fn";
        DiagnosticEngine diag;
        EXPECT_FALSE(pass.run(ir, diag));
        EXPECT_TRUE(has_code(diag, "E_IR_TYPE_MISMATCH"));
    }
}

/**
 * @brief Verify generated ServicesContract compiles zero-vtable sys-math/HAL structs and rejects invalid signatures.
 * @scenario Compile generated standalone header with (a) a zero-vtable struct using templated/overloaded methods and
 *           (b) a struct missing a required method.
 * @expected Case (a) compiles cleanly under C++17 and C++20 and computes the expected register value; Case (b) fails
 *           compilation due to static_assert in MotorPidFsmServicesContract.
 */
TEST(TypedServicesContractTest, CompileTimeServicesContract_AcceptsValidAndRejectsInvalid) {
    FsmIr ir = make_pid_controller_ir();
    fs::path temp_dir = fs::temp_directory_path() / "fsmc_typed_services_test";
    fs::create_directories(temp_dir);

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp17;
    opts.standalone = true;
    opts.freestanding = true;
    opts.target_namespace = "ctrl";
    const std::string code = CppGenerator::generate_header(ir, opts);

    EXPECT_NE(code.find("struct MotorPidFsmServicesContract"), std::string::npos);
    EXPECT_NE(code.find("reg.last_duty = srv.pid_step(in.setpoint, in.measured);"), std::string::npos);
    EXPECT_NE(code.find("srv.hal_apply_pwm(reg.last_duty);"), std::string::npos);

    fs::path hdr = temp_dir / "motor_pid_fsm.hpp";
    {
        std::ofstream os(hdr);
        os << code;
    }

    // Positive E2E: zero-vtable struct with template + overloaded methods (sys-math / HAL pattern)
    fs::path ok_drv = temp_dir / "ok_driver.cpp";
    {
        std::ofstream os(ok_drv);
        os << "#include \"motor_pid_fsm.hpp\"\n"
           << "struct FastHalServices {\n"
           << "    float applied = 0.0f;\n"
           << "    template <typename T>\n"
           << "    T pid_step(T sp, T meas) const noexcept { return (sp - meas) * T(2); }\n"
           << "    void hal_apply_pwm(float d) noexcept { applied = d; }\n"
           << "    void hal_apply_pwm(int d) noexcept { applied = static_cast<float>(d); }\n"
           << "};\n"
           << "int main() {\n"
           << "    using namespace ctrl;\n"
           << "    MotorPidFsmInPorts in{};\n"
           << "    in.setpoint = 10.0f;\n"
           << "    in.measured = 3.0f;\n"
           << "    MotorPidFsmOutPorts out{};\n"
           << "    MotorPidFsmRegisters reg{};\n"
           << "    FastHalServices srv{};\n"
           << "    MotorPidFsmWithServices<FastHalServices> sm(reg, srv);\n"
           << "    sm.dispatch(Step{}, in, out);\n"
           << "    if (!sm.is_in<Controlling>()) return 1;\n"
           << "    if (sm.registers().last_duty != 14.0f) return 2;\n"
           << "    if (srv.applied != 14.0f) return 3;\n"
           << "    return 0;\n"
           << "}\n";
    }

    for (const std::string& std_flag : {std::string("-std=c++17"), std::string("-std=c++20")}) {
        std::string exe = (temp_dir / ("ok_runner_" + std_flag.substr(5))).string();
        std::string cmd = "g++ " + std_flag + " -fno-exceptions -fno-rtti -Wall -Wextra -Werror -I" +
                          temp_dir.string() + " " + ok_drv.string() + " -o " + exe;
        ASSERT_EQ(std::system(cmd.c_str()), 0) << "Compile failed for " << std_flag;
        ASSERT_EQ(std::system(exe.c_str()), 0) << "Execution failed for " << std_flag;
    }

    // Negative compile-time check: invalid Services struct must fail static_assert
    fs::path bad_drv = temp_dir / "bad_driver.cpp";
    {
        std::ofstream os(bad_drv);
        os << "#include \"motor_pid_fsm.hpp\"\n"
           << "struct IncompleteServices {\n"
           << "    void hal_apply_pwm(float) noexcept {}\n"
           << "};\n"
           << "int main() {\n"
           << "    ctrl::MotorPidFsmWithServices<IncompleteServices> sm;\n"
           << "    (void)sm;\n"
           << "    return 0;\n"
           << "}\n";
    }
    std::string bad_cmd =
        "g++ -std=c++17 -fsyntax-only -I" + temp_dir.string() + " " + bad_drv.string() + " > /dev/null 2>&1";
    EXPECT_NE(std::system(bad_cmd.c_str()), 0) << "IncompleteServices must be rejected at compile time";

    fs::remove_all(temp_dir);
}

}  // namespace

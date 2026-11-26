/**
 * @file test_c99_backend.cpp
 * @brief E2E tests for the native Pure-C99 bare-metal backend (--target c99, zero C++ compiler on target).
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "fsm/backend/c99/c99_generator.hpp"
#include "fsm/backend/codegen_backend.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/middleend/pass_manager.hpp"

using namespace fsm::ir;
using namespace fsm::backend;
using namespace fsm::backend::c99;

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

}  // namespace

/**
 * @brief Verify C99CodegenBackend generates pure C99 header/source with zero C++ dependencies and compiles/links
 *        using ONLY a strict C99 compiler (-std=c99 -Wall -Wextra -Werror -pedantic).
 */
TEST(C99Backend, E2E_PureC99CompilesLinksAndRunsWithoutCpp) {
    const std::string cc = find_c_compiler();
    if (cc.empty()) {
        GTEST_SKIP() << "C compiler not found in PATH; skipping Pure-C99 E2E test.";
    }

    FsmIr ir;
    ir.name = "MotorCtrl";
    ir.initial_state = "Stopped";

    TypeDefinition sample_td("SensorSample", TypeKind::Struct);
    sample_td.fields.emplace_back("temp_c", DataType("int32_t"));
    sample_td.fields.emplace_back("ok", DataType("bool"));
    ir.add_type(std::move(sample_td));

    ir.add_service(ServiceMethodSignature("compute_duty", {ServiceParameter("temp", DataType("int32_t"))},
                                          DataType("int32_t"), /*pure=*/true, /*noexcept=*/true, "PID duty"));

    ir.add_state("Stopped");
    ir.add_state("Running");
    ir.add_state("TimedOut");

    ir.add_port(PortDefinition("sensor", "SensorSample", PortDirection::In));
    ir.add_port(PortDefinition("pwm_duty", "int32_t", PortDirection::Out));
    ir.add_variable(VariableDefinition("ticks", "int32_t", "0"));

    SignalDefinition start_sig("Start");
    start_sig.attributes.emplace_back("boost", "int32_t", "0");
    ir.add_signal(std::move(start_sig));
    ir.add_signal(SignalDefinition("Sample"));
    ir.add_signal(SignalDefinition("Stop"));

    ir.add_guard("sensor_ok", "", "sensor.ok && sensor.temp_c < 100", "sensor.ok && sensor.temp_c < 100");

    // Stopped -> Running on Start [sensor_ok] / pwm_duty = ev.boost
    {
        ActionSignature act("start_act");
        act.assignments.emplace_back(LValueTarget("pwm_duty", LValueScope::OutPort), "ev.boost");
        TransitionEdge t("Stopped", "Running", "Start");
        t.set_guard("sensor_ok");
        t.transition_action = std::move(act);
        ir.add_transition(std::move(t));
    }

    // Running -> Running (Internal) on Sample / pwm_duty = compute_duty(sensor.temp_c); ticks += 1;
    {
        ActionSignature act("sample_act");
        ActionCallOp call;
        call.function_name = "compute_duty";
        call.arguments = {"sensor.temp_c"};
        call.result_target = LValueTarget("pwm_duty", LValueScope::OutPort);
        act.instructions.emplace_back(std::move(call), "call compute_duty");
        act.assignments.emplace_back(LValueTarget("ticks", LValueScope::Register), "1", AssignmentOp::AddAssign);

        TransitionEdge t("Running", "Running", "Sample");
        t.kind = TransitionEdgeKind::Internal;
        t.transition_action = std::move(act);
        ir.add_transition(std::move(t));
    }

    // Running -> TimedOut after 50ms
    {
        TransitionEdge t("Running", "TimedOut", "");
        t.trigger = TimeTrigger(50U);
        ir.add_transition(std::move(t));
    }

    // TimedOut -> Stopped on Stop
    ir.add_transition(TransitionEdge("TimedOut", "Stopped", "Stop"));

    C99CodegenBackend backend(C99GeneratorOptions{"motor_ctrl.h"});
    EXPECT_EQ(backend.id(), "c99");
    EXPECT_EQ(backend.capabilities(), TargetCapabilities::c99_baremetal());

    fsm::diagnostic::DiagnosticEngine diag;
    const auto bundle = backend.emit_bundle(ir, diag);
    ASSERT_FALSE(diag.has_errors()) << diag.render_to_string();

    // Verify SHA-256 attestation
    const std::string expected_sha = FsmIrSerializer::compute_sha256(ir);
    EXPECT_NE(bundle.header.find("#define MOTOR_CTRL_IR_SHA256 \"" + expected_sha + "\""), std::string::npos);
    // Zero heap allocations
    EXPECT_EQ(bundle.source.find("malloc"), std::string::npos);
    EXPECT_EQ(bundle.source.find("free("), std::string::npos);

    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_pure_c99_backend_test";
    fs::create_directories(tmp_dir);

    {
        std::ofstream f(tmp_dir / "motor_ctrl.h");
        f << bundle.header;
    }
    {
        std::ofstream f(tmp_dir / "motor_ctrl.c");
        f << bundle.source;
    }
    {
        std::ofstream f(tmp_dir / "main.c");
        f << R"c(
#include <assert.h>
#include <string.h>
#include "motor_ctrl.h"

typedef struct {
    int32_t gain;
    int32_t calls;
} pid_ctx_t;

static int32_t my_compute_duty(void* user_data, int32_t temp) {
    pid_ctx_t* ctx = (pid_ctx_t*)user_data;
    ctx->calls += 1;
    return temp * ctx->gain;
}

int main(void) {
    pid_ctx_t ctx = { 3, 0 };
    motor_ctrl_services_t srv = { &ctx, my_compute_duty };
    motor_ctrl_fsm_t fsm;
    motor_ctrl_fsm_t restored;
    motor_ctrl_in_ports_t in_p;
    motor_ctrl_out_ports_t out_p;
    uint8_t snap[128];
    size_t written = 0;
    size_t read_sz = 0;

    memset(&in_p, 0, sizeof(in_p));
    memset(&out_p, 0, sizeof(out_p));

    motor_ctrl_init_with_services(&fsm, &srv);
    assert(motor_ctrl_current_state(&fsm) == MOTOR_CTRL_STATE_STOPPED);
    assert(strcmp(motor_ctrl_state_name(MOTOR_CTRL_STATE_STOPPED), "Stopped") == 0);
    assert(strlen(motor_ctrl_ir_sha256()) == 64);

    /* Guard rejected when sensor.ok == false */
    in_p.sensor.temp_c = 25;
    in_p.sensor.ok = false;
    assert(motor_ctrl_dispatch_start(&fsm, 40, &in_p, &out_p) == MOTOR_CTRL_STATUS_GUARD_REJECTED);
    assert(motor_ctrl_current_state(&fsm) == MOTOR_CTRL_STATE_STOPPED);

    /* Guard passes when sensor.ok == true && temp < 100 */
    in_p.sensor.ok = true;
    assert(motor_ctrl_dispatch_start(&fsm, 40, &in_p, &out_p) == MOTOR_CTRL_STATUS_OK);
    assert(motor_ctrl_current_state(&fsm) == MOTOR_CTRL_STATE_RUNNING);
    assert(out_p.pwm_duty == 40);

    /* Internal transition invokes C service callback & increments register */
    assert(motor_ctrl_dispatch_sample(&fsm, &in_p, &out_p) == MOTOR_CTRL_STATUS_OK);
    assert(ctx.calls == 1);
    assert(out_p.pwm_duty == 75);
    assert(fsm.regs.ticks == 1);

    /* Advance 30ms (still Running) and snapshot */
    assert(motor_ctrl_tick(&fsm, 30, &in_p, &out_p) == MOTOR_CTRL_STATUS_IGNORED);
    assert(motor_ctrl_current_state(&fsm) == MOTOR_CTRL_STATE_RUNNING);
    assert(motor_ctrl_save_snapshot(&fsm, snap, sizeof(snap), &written));
    assert(written == motor_ctrl_snapshot_size(&fsm));

    /* Advance 25ms -> expires 50ms timeout */
    assert(motor_ctrl_tick(&fsm, 25, &in_p, &out_p) == MOTOR_CTRL_STATUS_OK);
    assert(motor_ctrl_current_state(&fsm) == MOTOR_CTRL_STATE_TIMED_OUT);

    /* Corrupted snapshot must fail checksum and leave state intact */
    snap[written - 1] ^= 0xFFU;
    assert(!motor_ctrl_restore_snapshot(&fsm, snap, written, &read_sz));
    assert(motor_ctrl_current_state(&fsm) == MOTOR_CTRL_STATE_TIMED_OUT);
    snap[written - 1] ^= 0xFFU;

    /* Valid restore brings fresh instance back to Running with 30ms elapsed and ticks == 1 */
    motor_ctrl_init_with_services(&restored, &srv);
    assert(motor_ctrl_restore_snapshot(&restored, snap, written, &read_sz));
    assert(read_sz == written);
    assert(motor_ctrl_current_state(&restored) == MOTOR_CTRL_STATE_RUNNING);
    assert(restored.regs.ticks == 1);
    assert(motor_ctrl_tick(&restored, 20, &in_p, &out_p) == MOTOR_CTRL_STATUS_OK);
    assert(motor_ctrl_current_state(&restored) == MOTOR_CTRL_STATE_TIMED_OUT);

    motor_ctrl_destroy(&restored);
    motor_ctrl_destroy(&fsm);
    return 0;
}
)c";
    }

    // Compile and link strictly with the C compiler ONLY (zero C++ involved!)
    const fs::path bin = tmp_dir / "pure_c99_runner";
    const std::string cmd = cc +
                            " -std=c99 -Wall -Wextra -Werror -pedantic -Wstrict-prototypes -Wmissing-prototypes "
                            "-Wshadow -I\"" +
                            tmp_dir.string() + "\" \"" + (tmp_dir / "motor_ctrl.c").string() + "\" \"" +
                            (tmp_dir / "main.c").string() + "\" -o \"" + bin.string() + "\"";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << "Pure C99 compilation failed:\n" << cmd;
    ASSERT_EQ(std::system(("\"" + bin.string() + "\"").c_str()), 0) << "Pure C99 runner failed";

    fs::remove_all(tmp_dir);
}

/**
 * @brief Verify external_ref lowers cleanly to #include "<module>.h" and C99 typedef alias.
 */
TEST(C99Backend, LowersExternalTypeRefToC99IncludeAndTypedef) {
    FsmIr ir;
    ir.name = "BusNode";
    ir.initial_state = "Idle";
    ir.add_type(TypeDefinition::make_external("CanFrame", "drivers/can_types.hpp", "drivers.can_frame_t",
                                              TypeKind::Struct, "Shared CAN frame"));
    ir.add_state("Idle");
    ir.add_port(PortDefinition("rx", "CanFrame", PortDirection::In));

    const auto bundle = C99Generator::generate(ir);
    EXPECT_NE(bundle.header.find("#include \"drivers/can_types.h\""), std::string::npos);
    EXPECT_NE(bundle.header.find("typedef can_frame_t bus_node_can_frame_t;"), std::string::npos);
    EXPECT_NE(bundle.header.find("bus_node_can_frame_t rx;"), std::string::npos);
}

/**
 * @brief Verify SignalEmitOp generates a zero-heap static outbox on OutPorts in Pure-C99 and executes cleanly under
 * strict embedded C99 warning flags.
 * @scenario Build an FSM with a SignalEmitOp carrying a typed payload over an output port, generate Pure-C99, compile
 * with -std=c99 -Wall -Wextra -Werror -pedantic -Wstrict-prototypes -Wmissing-prototypes -Wshadow, and verify outbox.
 * @expected Emitted event tag, port name, and payload match in out_ports.outbox and reset cleanly on the next cycle.
 */
TEST(C99Backend, SignalEmitOp_PopulatesStaticOutboxUnderStrictC99) {
    const std::string cc = find_c_compiler();
    if (cc.empty()) {
        GTEST_SKIP() << "No C compiler found on PATH";
    }

    FsmIr ir;
    ir.name = "AlertNode";
    ir.initial_state = "Normal";
    ir.add_state("Normal");
    ir.add_state("Fault");
    ir.add_port(PortDefinition("temp_c", DataType::int32(), PortDirection::In));
    ir.add_signal(SignalDefinition("EvSample"));
    SignalDefinition ev_alert("EvOverTemp");
    ev_alert.attributes.push_back(SignalAttribute{"peak_temp", DataType::int32()});
    ir.add_signal(ev_alert);

    TransitionEdge t("Normal", "Fault", "EvSample");
    t.guard = "temp_c >= 90";
    ActionSignature act("EmitOverTemp");
    SignalEmitOp se;
    se.signal_name = "EvOverTemp";
    se.target_port = "alert_bus";
    se.arguments = {"temp_c"};
    act.instructions.emplace_back(se, "emit overtemp");
    t.transition_action = act;
    ir.add_transition(t);

    const auto bundle = C99Generator::generate(ir);
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_test_c99_outbox";
    fs::create_directories(tmp_dir);
    {
        std::ofstream(tmp_dir / "alert_node.h") << bundle.header;
        std::ofstream(tmp_dir / "alert_node.c") << bundle.source;
        std::ofstream(tmp_dir / "main.c") << R"c(
#include <assert.h>
#include <string.h>
#include "alert_node.h"

int main(void) {
    alert_node_fsm_t fsm;
    alert_node_in_ports_t in_p;
    alert_node_out_ports_t out_p;
    alert_node_init(&fsm);

    memset(&in_p, 0, sizeof(in_p));
    memset(&out_p, 0, sizeof(out_p));
    in_p.temp_c = 50;
    assert(alert_node_dispatch_ev_sample(&fsm, &in_p, &out_p) == ALERT_NODE_STATUS_GUARD_REJECTED);
    assert(out_p.outbox_count == 0U);

    in_p.temp_c = 105;
    assert(alert_node_dispatch_ev_sample(&fsm, &in_p, &out_p) == ALERT_NODE_STATUS_OK);
    assert(alert_node_current_state(&fsm) == ALERT_NODE_STATE_FAULT);
    assert(out_p.outbox_count == 1U);
    assert(out_p.outbox[0].tag == ALERT_NODE_OUT_EV_OVER_TEMP);
    assert(strcmp(out_p.outbox[0].port, "alert_bus") == 0);
    assert(out_p.outbox[0].ev_over_temp.peak_temp == 105);

    /* Next cycle resets outbox cleanly */
    assert(alert_node_dispatch_ev_sample(&fsm, &in_p, &out_p) == ALERT_NODE_STATUS_IGNORED);
    assert(out_p.outbox_count == 0U);
    return 0;
}
)c";
    }

    const fs::path bin = tmp_dir / "outbox_runner";
    const std::string cmd = cc +
                            " -std=c99 -Wall -Wextra -Werror -pedantic -Wstrict-prototypes -Wmissing-prototypes "
                            "-Wshadow -I\"" +
                            tmp_dir.string() + "\" \"" + (tmp_dir / "alert_node.c").string() + "\" \"" +
                            (tmp_dir / "main.c").string() + "\" -o \"" + bin.string() + "\"";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << "Strict C99 outbox compilation failed:\n" << cmd;
    ASSERT_EQ(std::system(("\"" + bin.string() + "\"").c_str()), 0) << "Strict C99 outbox runner failed";

    fs::remove_all(tmp_dir);
}

/**
 * @brief Verify C99 backend expands compound guards, synthesizes symbolic guard callbacks, runs native zero-heap
 *        deferred event recall, and omits #include "<prefix>.h" in single-TU stdout emit().
 */
TEST(C99Backend, CompoundGuardsDeferredRecallAndSingleTuEmit_StrictC99) {
    FsmIr ir;
    ir.name = "C99AuditFsm";
    ir.initial_state = "Busy";

    ir.add_state("Busy");
    ir.add_state("Ready");
    ir.add_state("Done");
    ir.find_state_mut("Busy")->deferred_events.push_back("Work");

    ir.add_signal(SignalDefinition("Finish"));
    ir.add_signal(SignalDefinition("Work"));
    ir.add_guard("g_ok", "", "", "");
    ir.add_guard("g_safe", "", "", "");

    {
        TransitionEdge t("Busy", "Ready", "Finish");
        t.set_guard("fsm::and_<g_ok, g_safe>");
        ir.add_transition(std::move(t));
    }
    {
        TransitionEdge t("Ready", "Done", "Work");
        ir.add_transition(std::move(t));
    }

    auto pm = fsm::middleend::PassManager::create_lowering_pipeline(TargetCapabilities::c99_baremetal());
    fsm::diagnostic::DiagnosticEngine diag;
    ASSERT_TRUE(pm.run(ir, diag));

    C99CodegenBackend backend;
    const std::string single_tu = backend.emit(ir, diag);
    EXPECT_FALSE(diag.has_errors());
    EXPECT_EQ(single_tu.find("#include \"c99_audit_fsm.h\""), std::string::npos);
    EXPECT_NE(single_tu.find("bool (*g_ok)(void* user_data);"), std::string::npos);
    EXPECT_NE(single_tu.find("bool (*g_safe)(void* user_data);"), std::string::npos);
    EXPECT_EQ(single_tu.find("fsm::and_"), std::string::npos);
    EXPECT_NE(single_tu.find("deferred_queue"), std::string::npos);
    EXPECT_NE(single_tu.find("c99_audit_fsm_recall_deferred"), std::string::npos);

    const std::string cc = find_c_compiler();
    if (cc.empty()) {
        return;
    }

    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_c99_guards_deferred_test";
    fs::create_directories(tmp_dir);
    const auto bundle = backend.emit_bundle(ir, diag);
    {
        std::ofstream hf(tmp_dir / "c99_audit_fsm.h");
        hf << bundle.header;
        std::ofstream cf(tmp_dir / "c99_audit_fsm.c");
        cf << bundle.source;
        std::ofstream mf(tmp_dir / "main.c");
        mf << R"c(
#include <assert.h>
#include <stdbool.h>
#include "c99_audit_fsm.h"

static bool cb_ok(void* ud) { (void)ud; return true; }
static bool cb_safe(void* ud) { (void)ud; return true; }

int main(void) {
    c99_audit_fsm_fsm_t fsm;
    c99_audit_fsm_services_t srv = { NULL, cb_ok, cb_safe };
    c99_audit_fsm_init_with_services(&fsm, &srv);
    assert(c99_audit_fsm_current_state(&fsm) == C99_AUDIT_FSM_STATE_BUSY);
    assert(c99_audit_fsm_dispatch_work(&fsm, NULL, NULL) == C99_AUDIT_FSM_STATUS_OK);
    assert(c99_audit_fsm_current_state(&fsm) == C99_AUDIT_FSM_STATE_BUSY);
    assert(c99_audit_fsm_dispatch_finish(&fsm, NULL, NULL) == C99_AUDIT_FSM_STATUS_OK);
    assert(c99_audit_fsm_current_state(&fsm) == C99_AUDIT_FSM_STATE_DONE);
    return 0;
}
)c";
    }

    const fs::path bin = tmp_dir / "deferred_runner";
    const std::string cmd = cc + " -std=c99 -Wall -Wextra -Werror -pedantic -I\"" + tmp_dir.string() + "\" \"" +
                            (tmp_dir / "c99_audit_fsm.c").string() + "\" \"" + (tmp_dir / "main.c").string() +
                            "\" -o \"" + bin.string() + "\"";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << "Strict C99 compile failed:\n" << cmd;
    ASSERT_EQ(std::system(("\"" + bin.string() + "\"").c_str()), 0);
    fs::remove_all(tmp_dir);
}

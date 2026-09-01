#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "fsm/backend/cpp/cpp_model_emitter.hpp"
#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"
#include "fsm/middleend/pass_manager.hpp"

namespace {

constexpr const char* kFlightControlSysMLv2 = R"(
package FlightControlSystem {
    event def TakeoffCmd {
        attribute target_alt_m : Real;
    }
    event def EmergencyStopCmd;

    state def FlightMissionController {
        in port battery_percent : Real { assert constraint { self >= 0.0 and self <= 100.0 } }
        in port altitude_m      : Real { assert constraint { self >= 0.0 and self <= 5000.0 } }
        in port has_gps_lock    : Boolean;

        out port motor_thrust   : Real { assert constraint { self >= 0.0 and self <= 100.0 } }
        out port failsafe_cmd   : Boolean;

        attribute retry_counter : Integer = 0;

        action def SendIotAlert;

        entry; then Preflight;

        state Preflight {
            transition takeoff
                accept cmd : TakeoffCmd
                if has_gps_lock == true and cmd.target_alt_m >= 10.0
                do {
                    motor_thrust = 50.0;
                    retry_counter = 0;
                }
                then InFlight;
        }

        state InFlight {
            transition low_battery_critical
                if battery_percent < 20.0
                do {
                    failsafe_cmd = true;
                    motor_thrust = 15.0;
                }
                then ReturnToHome;

            transition on_emergency
                accept EmergencyStopCmd
                do SendIotAlert
                then Terminated;
        }

        state ReturnToHome;
        state Terminated;
    }
}
)";

// ============================================================================
// Concrete Runtime Types corresponding to the Spec-Driver Model
// ============================================================================

struct TakeoffCmd {
    static constexpr std::string_view name = "TakeoffCmd";
    double target_alt_m{0.0};
    constexpr explicit TakeoffCmd(double alt) : target_alt_m(alt) {}
};

struct EmergencyStopCmd {
    static constexpr std::string_view name = "EmergencyStopCmd";
};

// States
struct Preflight {
    static constexpr std::string_view name = "Preflight";
};
struct InFlight {
    static constexpr std::string_view name = "InFlight";
};
struct ReturnToHome {
    static constexpr std::string_view name = "ReturnToHome";
};
struct Terminated {
    static constexpr std::string_view name = "Terminated";
};

// Domain Structures
struct FlightInPorts {
    double battery_percent{100.0};
    double altitude_m{0.0};
    bool has_gps_lock{false};
};

struct FlightOutPorts {
    double motor_thrust{0.0};
    bool failsafe_cmd{false};
};

struct FlightRegisters {
    int retry_counter{0};
};

struct MockFlightServices {
    bool alert_sent{false};
    void SendIotAlert() { alert_sent = true; }
};

// Guards
struct TakeoffGuard {
    [[nodiscard]] constexpr bool operator()(const TakeoffCmd& cmd, const FlightInPorts& in,
                                            const FlightRegisters& /*reg*/) const noexcept {
        return in.has_gps_lock && cmd.target_alt_m >= 10.0;
    }
};

struct LowBatteryCriticalGuard {
    [[nodiscard]] constexpr bool operator()(const FlightInPorts& in, const FlightRegisters& /*reg*/) const noexcept {
        return in.battery_percent < 20.0;
    }
    [[nodiscard]] constexpr bool operator()(const FlightInPorts& in) const noexcept {
        return in.battery_percent < 20.0;
    }
};

// Actions
struct TakeoffAction {
    void operator()(const TakeoffCmd& /*cmd*/, FlightOutPorts& out, FlightRegisters& reg) const {
        out.motor_thrust = 50.0;
        reg.retry_counter = 0;
    }
};

struct LowBatteryCriticalAction {
    void operator()(FlightOutPorts& out, FlightRegisters& /*reg*/) const {
        out.failsafe_cmd = true;
        out.motor_thrust = 15.0;
    }
};

struct SendIotAlertAction {
    template <typename Services>
    auto operator()(Services& srv) const -> decltype(srv.SendIotAlert()) {
        srv.SendIotAlert();
    }
};

// Transition Table definition
using FlightControlTable =
    fsm::transition_table<fsm::row<Preflight, TakeoffCmd, InFlight>::when<TakeoffGuard>::then<TakeoffAction>,
                          fsm::row<InFlight, fsm::anonymous_event,
                                   ReturnToHome>::when<LowBatteryCriticalGuard>::then<LowBatteryCriticalAction>,
                          fsm::row<InFlight, EmergencyStopCmd, Terminated>::then<SendIotAlertAction>>;

using FlightControlFSM =
    fsm::fsm<FlightControlTable, FlightInPorts, FlightOutPorts, FlightRegisters, MockFlightServices, Preflight>;

}  // namespace

// ============================================================================
// Unit Tests
// ============================================================================

TEST(SysML2FlightControlTest, ParseFlightMissionControllerSysMLv2) {
    fsm::codegen::FsmIr model;
    std::string error;
    fsm::codegen::Sysml2Parser parser;

    bool ok = parser.parse(kFlightControlSysMLv2, model, error);
    ASSERT_TRUE(ok) << "Parser error: " << error;

    EXPECT_EQ(model.name, "FlightMissionController");
    EXPECT_EQ(model.ns, "FlightControlSystem");

    // Ports
    ASSERT_EQ(model.ports.size(), 5);
    const auto* bp = model.find_port("battery_percent");
    ASSERT_NE(bp, nullptr);
    EXPECT_TRUE(bp->is_in());
    EXPECT_TRUE(bp->min_value.has_value());
    EXPECT_DOUBLE_EQ(bp->min_value.value(), 0.0);
    EXPECT_TRUE(bp->max_value.has_value());
    EXPECT_DOUBLE_EQ(bp->max_value.value(), 100.0);

    const auto* mt = model.find_port("motor_thrust");
    ASSERT_NE(mt, nullptr);
    EXPECT_TRUE(mt->is_out());
    EXPECT_DOUBLE_EQ(mt->max_value.value(), 100.0);

    const auto* gps = model.find_port("has_gps_lock");
    ASSERT_NE(gps, nullptr);
    EXPECT_TRUE(gps->is_in());

    // Registers / Variables
    ASSERT_EQ(model.variables.size(), 1);
    EXPECT_EQ(model.variables[0].name, "retry_counter");
    EXPECT_EQ(model.variables[0].initial_value, "0");

    // Events / Signals
    EXPECT_TRUE(
        model.find_signal("TakeoffCmd") != nullptr ||
        std::any_of(model.events.begin(), model.events.end(), [](const auto& e) { return e.name == "TakeoffCmd"; }));
    EXPECT_TRUE(model.find_signal("EmergencyStopCmd") != nullptr ||
                std::any_of(model.events.begin(), model.events.end(),
                            [](const auto& e) { return e.name == "EmergencyStopCmd"; }));

    // States
    EXPECT_NE(model.find_state("Preflight"), nullptr);
    EXPECT_NE(model.find_state("InFlight"), nullptr);
    EXPECT_NE(model.find_state("ReturnToHome"), nullptr);
    EXPECT_NE(model.find_state("Terminated"), nullptr);

    // Actions
    EXPECT_TRUE(std::any_of(model.actions.begin(), model.actions.end(),
                            [](const auto& a) { return a.name == "SendIotAlert"; }));
}

TEST(SysML2FlightControlTest, VerificationPassesAndIntervalAnalysis) {
    fsm::codegen::FsmIr model;
    std::string error;
    fsm::codegen::Sysml2Parser parser;
    ASSERT_TRUE(parser.parse(kFlightControlSysMLv2, model, error));

    fsm::codegen::DiagnosticEngine diag;
    fsm::codegen::EFSMIntervalAnalyzer interval_analyzer(model);
    auto findings = interval_analyzer.analyze(diag);

    // The spec is consistent and valid -> no contract violations
    EXPECT_FALSE(diag.has_errors());

    auto pm = fsm::codegen::PassManager::create_default_pipeline();
    bool passes_ok = pm.run(model, diag);
    EXPECT_TRUE(passes_ok);
    EXPECT_FALSE(diag.has_errors());
}

TEST(SysML2FlightControlTest, CppCodeGeneration) {
    fsm::codegen::FsmIr model;
    std::string error;
    fsm::codegen::Sysml2Parser parser;
    ASSERT_TRUE(parser.parse(kFlightControlSysMLv2, model, error));

    std::ostringstream ss;
    fsm::codegen::GeneratorOptions opts;
    opts.cpp_standard = fsm::codegen::CppStandard::Cpp20;
    opts.include_stubs = true;
    fsm::codegen::CppModelEmitter::emit_model(ss, model, opts);

    std::string generated_code = ss.str();
    EXPECT_NE(generated_code.find("FlightMissionControllerInPorts"), std::string::npos);
    EXPECT_NE(generated_code.find("FlightMissionControllerOutPorts"), std::string::npos);
    EXPECT_NE(generated_code.find("FlightMissionControllerRegisters"), std::string::npos);
    EXPECT_NE(generated_code.find("FlightMissionControllerServices"), std::string::npos);
    EXPECT_NE(generated_code.find("TakeoffCmd"), std::string::npos);
    EXPECT_NE(generated_code.find("SendIotAlert"), std::string::npos);
}

TEST(SysML2FlightControlTest, RuntimeDualParadigmExecution) {
    MockFlightServices services;
    FlightControlFSM controller(services);

    EXPECT_TRUE(controller.is_in_state<Preflight>());
    EXPECT_EQ(controller.current_state_name(), "Preflight");

    FlightInPorts in;
    FlightOutPorts out;

    // 1. Takeoff with no GPS lock -> Guard rejected, stays Preflight
    in.has_gps_lock = false;
    TakeoffCmd cmd_low(5.0);
    auto res1 = controller.dispatch(cmd_low, in, out);
    EXPECT_TRUE(res1.is_guard_rejected());
    EXPECT_TRUE(controller.is_in_state<Preflight>());
    EXPECT_DOUBLE_EQ(out.motor_thrust, 0.0);

    // 2. Takeoff with GPS lock and alt >= 10.0 -> Success, InFlight, thrust = 50
    in.has_gps_lock = true;
    TakeoffCmd cmd_ok(100.0);
    auto res2 = controller.dispatch(cmd_ok, in, out);
    EXPECT_TRUE(res2.is_success());
    EXPECT_TRUE(controller.is_in_state<InFlight>());
    EXPECT_DOUBLE_EQ(out.motor_thrust, 50.0);
    EXPECT_EQ(controller.registers().retry_counter, 0);

    // 3. Periodic sampled control loop: step(in, out) with healthy battery (85%) -> Steady (no transition)
    in.battery_percent = 85.0;
    auto step_res1 = controller.step(in, out);
    EXPECT_TRUE(step_res1.is_steady());
    EXPECT_FALSE(step_res1.has_transitioned());
    EXPECT_TRUE(controller.is_in_state<InFlight>());
    EXPECT_DOUBLE_EQ(out.motor_thrust, 50.0);
    EXPECT_FALSE(out.failsafe_cmd);

    // 4. Periodic sampled control loop: step(in, out) with critical battery (15%) -> ReturnToHome
    in.battery_percent = 15.0;
    auto step_res2 = controller.step(in, out);
    EXPECT_TRUE(step_res2.has_transitioned());
    EXPECT_FALSE(step_res2.is_steady());
    EXPECT_TRUE(controller.is_in_state<ReturnToHome>());
    EXPECT_TRUE(out.failsafe_cmd);
    EXPECT_DOUBLE_EQ(out.motor_thrust, 15.0);
}

TEST(SysML2FlightControlTest, RuntimeEmergencyStopExecution) {
    MockFlightServices services;
    FlightControlFSM controller(services);

    FlightInPorts in;
    FlightOutPorts out;
    in.has_gps_lock = true;

    // Takeoff to InFlight
    controller.dispatch(TakeoffCmd(50.0), in, out);
    ASSERT_TRUE(controller.is_in_state<InFlight>());
    EXPECT_FALSE(services.alert_sent);

    // Reactive Emergency Stop Command
    auto res = controller.dispatch(EmergencyStopCmd{}, in, out, services);
    EXPECT_TRUE(res.is_success());
    EXPECT_TRUE(controller.is_in_state<Terminated>());
    EXPECT_TRUE(services.alert_sent);
}

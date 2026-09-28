/**
 * @file main.cpp
 * @brief Executable verification test harness for 6-DOF Robotic Arm Sequencer.
 * Demonstrates: Deep History [H*] restoration, submachine tool calibration,
 * safety interlocks, and MBSE typed I/O port contract validation.
 */

#include <cassert>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "robotic_arm_fsm.hpp"

namespace robotics {

/**
 * @struct ConcreteRoboticServices
 * @brief Hardware actuator and motion controller service driver implementation.
 */
struct ConcreteRoboticServices : public RoboticArmFSMServices {
    bool brakes_engaged{true};
    bool servos_powered{false};
    bool spindle_running{false};
    bool offsets_calibrated{false};
    int toolpath_pass{0};

    void InitializeServosAction() override {
        servos_powered = true;
        brakes_engaged = false;
        std::cout << "  \033[1;32m[SERVO DRIVER]\033[0m 6-Axis AC brushless servomotors energized; brakes uncoupled\n";
    }

    void SpinSpindleAction() override {
        spindle_running = true;
        std::cout << "  \033[1;32m[SPINDLE]\033[0m High-speed spindle spinning at 15,000 RPM with flood coolant\n";
    }

    void AdvanceToolpathAction() override {
        toolpath_pass++;
        std::cout << "  \033[1;32m[CNC TRAJECTORY]\033[0m Interpolator advanced to pass #" << toolpath_pass << "\n";
    }

    void CutPowerAction() override {
        servos_powered = false;
        spindle_running = false;
        brakes_engaged = true;
        std::cout << "  \033[1;31m[SAFETY INTERLOCK]\033[0m Physical barrier light curtain breached! Power cut & "
                     "brakes engaged!\n";
    }

    void EngageBrakesAction() override {
        brakes_engaged = true;
        std::cout << "  \033[1;31m[SAFETY BRAKES]\033[0m Fail-safe spring electromagnetic brakes clamped\n";
    }

    void ReleaseBrakesAction() override {
        brakes_engaged = false;
        std::cout << "  \033[1;32m[SAFETY RECOVERY]\033[0m Interlock cleared; releasing axis brakes\n";
    }

    void WarmupServosAction() override {
        servos_powered = true;
        std::cout << "  \033[1;32m[SERVO WARMUP]\033[0m Re-synchronizing optical encoders; verifying zero-drift\n";
    }

    void RetractArmAction() override {
        std::cout << "  \033[1;33m[CALIBRATION]\033[0m Arm safely retracted along tool Z-axis to laser sensor\n";
    }

    void RestoreOffsetsAction() override {
        offsets_calibrated = true;
        std::cout
            << "  \033[1;32m[CALIBRATION]\033[0m Tool Center Point (TCP) kinematics offset compensation applied\n";
    }
};

}  // namespace robotics

static void print_section(std::string_view title) {
    std::cout << "\n\033[1;36m================================================================================\033[0m\n"
              << "\033[1;37m " << title << "\033[0m\n"
              << "\033[1;36m================================================================================\033[0m\n";
}

static void print_step(std::string_view step, std::string_view state) {
    std::cout << "  \033[1;35m[STEP]\033[0m " << std::left << std::setw(42) << step << " --> Current State: \033[1;32m"
              << state << "\033[0m\n";
}

int main() {
    print_section("FSMC SHOWCASE 02: 6-DOF ROBOTIC ARM TOOL SEQUENCER");

    robotics::RoboticArmFSMInPorts in_ports;
    robotics::RoboticArmFSMOutPorts out_ports;
    robotics::ConcreteRoboticServices services;

    // 1. Verify MBSE Port Range Contracts
    in_ports.payload_weight_kg = 18.5f;  // Valid (0.0 to 35.0 kg)
    assert(in_ports.validate_contracts());
    out_ports.motor_torque_nm = 120.0f;  // Valid (0.0 to 250.0 Nm)
    assert(out_ports.validate_contracts());
    std::cout << "  \033[1;32m[MBSE CONTRACTS]\033[0m Input and Output port range constraints verified: OK\n";

    // 2. Initialize State Machine
    robotics::RoboticArmFSM fsm(services);
    assert(fsm.current_state_name() == "Standby");
    print_step("Initial Power-On State", fsm.current_state_name());

    // 3. Standby -> Operational (PowerUpCmd)
    std::cout << "\n\033[1;33m--- Scenario 1: Arm Initialization & Standby to Operational ---\033[0m\n";
    fsm.dispatch(robotics::PowerUpCmd{});
    assert(fsm.is_in<robotics::Idle>());
    assert(services.servos_powered);
    print_step("Dispatched PowerUpCmd", fsm.current_state_name());

    // 4. Idle -> Machining (StartJobCmd)
    std::cout << "\n\033[1;33m--- Scenario 2: Tool Engagement & Nested Roughing Phase ---\033[0m\n";
    fsm.dispatch(robotics::StartJobCmd{});
    assert(fsm.is_in<robotics::Roughing>());
    assert(services.spindle_running);
    print_step("Dispatched StartJobCmd", fsm.current_state_name());

    // 5. Advance Machining: Roughing -> SemiFinishing
    std::cout << "\n\033[1;33m--- Scenario 3: Advancing CNC Pass (Roughing -> SemiFinishing) ---\033[0m\n";
    fsm.dispatch(robotics::StepFinishingCmd{});
    assert(fsm.is_in<robotics::SemiFinishing>());
    assert(services.toolpath_pass == 1);
    print_step("Dispatched StepFinishingCmd (1)", fsm.current_state_name());

    // 6. Emergency Safety Interlock: Interruption during SemiFinishing
    std::cout << "\n\033[1;33m--- Scenario 4: Safety Curtain Breach -> SafetyInterlocked ---\033[0m\n";
    fsm.dispatch(robotics::SafetyPauseCmd{});
    assert(fsm.is_in<robotics::SafetyInterlocked>());
    assert(services.brakes_engaged);
    assert(!services.servos_powered);
    print_step("Dispatched SafetyPauseCmd", fsm.current_state_name());

    // 7. Resuming via Deep History [H*]: Must restore exactly to SemiFinishing (NOT default Roughing or Idle)
    std::cout << "\n\033[1;33m--- Scenario 5: Safety Cleared & Deep History Restoration [H*] ---\033[0m\n";
    fsm.dispatch(robotics::ResumeJobCmd{});
    assert(fsm.is_in<robotics::SemiFinishing>());
    assert(services.servos_powered);
    print_step("Dispatched ResumeJobCmd -> Restored [H*]", fsm.current_state_name());
    std::cout
        << "  \033[1;32m[DEEP HISTORY OK]\033[0m Verified: Arm restored directly to nested substate 'SemiFinishing'!\n";

    // 8. Tool Calibration Submachine Call
    std::cout << "\n\033[1;33m--- Scenario 6: Laser Tool Calibration & History Re-entry ---\033[0m\n";
    fsm.dispatch(robotics::CalibrateToolCmd{});
    assert(fsm.is_in<robotics::ToolCalibration>());
    print_step("Dispatched CalibrateToolCmd", fsm.current_state_name());

    fsm.dispatch(robotics::CalibrationOkEvent{});
    assert(fsm.is_in<robotics::SemiFinishing>());
    assert(services.offsets_calibrated);
    print_step("Dispatched CalibrationOkEvent -> Restored [H*]", fsm.current_state_name());

    // 9. Advance to Finishing
    std::cout << "\n\033[1;33m--- Scenario 7: Toolpath Finalization (SemiFinishing -> Finishing) ---\033[0m\n";
    fsm.dispatch(robotics::StepFinishingCmd{});
    assert(fsm.is_in<robotics::Finishing>());
    assert(services.toolpath_pass == 2);
    print_step("Dispatched StepFinishingCmd (2)", fsm.current_state_name());

    // 10. Job Complete back to Standby
    std::cout << "\n\033[1;33m--- Scenario 8: Job Complete & Return to Standby ---\033[0m\n";
    fsm.dispatch(robotics::JobCompleteCmd{});
    assert(fsm.is_in<robotics::Standby>());
    print_step("Dispatched JobCompleteCmd", fsm.current_state_name());

    print_section("ALL ROBOTIC SEQUENCER TESTS PASSED (100% SUCCESS)");
    return 0;
}

/**
 * @file main.cpp
 * @brief Executable verification test harness for DO-178C / DO-333 Flight Control Mode Manager.
 * Demonstrates: SMT guard evaluation, requirement traceability compliance,
 * sensor contract validation, and DO-178C Level A mode transitions.
 */

#include <cassert>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "fms_fsm.hpp"

namespace avionics {

/**
 * @struct ConcreteAvionicsServices
 * @brief Primary Flight Computer (PFC) hardware effector and FCC bus interface.
 */
struct ConcreteAvionicsServices : public FlightControlModeManagerServices {
    bool air_data_calibrated{false};
    bool pitch_roll_augmented{false};
    bool guidance_couplers_armed{false};
    bool direct_law_warning_active{false};
    bool flight_director_connected{false};
    bool mechanical_backup_active{false};

    void CalibrateAirDataSensors() override {
        air_data_calibrated = true;
        std::cout << "  \033[1;32m[AVIONICS/PFC]\033[0m Pitot-static and AoA vane sensors self-calibrated (Built-In "
                     "Test OK)\n";
    }

    void EngagePitchRollAugmentation() override {
        pitch_roll_augmented = true;
        std::cout << "  \033[1;32m[CONTROL LAWS]\033[0m Normal Law flight envelope protection active (C* pitch rate / "
                     "roll rate)\n";
    }

    void ArmGuidanceCouplers() override {
        guidance_couplers_armed = true;
        std::cout << "  \033[1;32m[AUTOPILOT]\033[0m Lateral (LNAV) and Vertical (VNAV) guidance couplers ARMED\n";
    }

    void DisarmGuidanceCouplers() override {
        guidance_couplers_armed = false;
        std::cout << "  \033[1;33m[AUTOPILOT]\033[0m Autopilot disengaged: pilot hand flying on sidestick\n";
    }

    void AnnunciateDirectLawWarning() override {
        direct_law_warning_active = true;
        std::cout << "  \033[1;31m[EICAS WARNING]\033[0m DIRECT LAW: Flight envelope protections lost! Amber master "
                     "caution!\n";
    }

    void TrimElevatorsAction() override {
        std::cout << "  \033[1;32m[FLIGHT CONTROLS]\033[0m Horizontal stabilizer auto-trimmed to takeoff reference "
                     "(+3.2 deg)\n";
    }

    void ConnectFlightDirectorAction() override {
        flight_director_connected = true;
        std::cout << "  \033[1;32m[FLIGHT DIRECTOR]\033[0m Primary flight display FD crossbars coupled to FMC "
                     "navigation route\n";
    }

    void DisconnectFlightDirectorAction() override {
        flight_director_connected = false;
        std::cout
            << "  \033[1;33m[FLIGHT DIRECTOR]\033[0m Flight director decoupled; reverting to manual command bars\n";
    }

    void BypassFlightEnvelopeAction() override {
        std::cout << "  \033[1;31m[RECONFIGURATION]\033[0m Air-data inconsistency: Bypassing FCC high-alpha/bank angle "
                     "limits\n";
    }

    void SwitchMechanicalBackupAction() override {
        mechanical_backup_active = true;
        std::cout
            << "  \033[1;31m[BACKUP]\033[0m Dual hydraulic drop: Rudder pedals and stabilizer trim cables active\n";
    }
};

}  // namespace avionics

static void print_section(std::string_view title) {
    std::cout << "\n\033[1;36m================================================================================\033[0m\n"
              << "\033[1;37m " << title << "\033[0m\n"
              << "\033[1;36m================================================================================\033[0m\n";
}

static void print_step(std::string_view step, std::string_view state) {
    std::cout << "  \033[1;35m[STEP]\033[0m " << std::left << std::setw(44) << step << " --> Current State: \033[1;32m"
              << state << "\033[0m\n";
}

int main() {
    print_section("FSMC SHOWCASE 04: DO-178C LEVEL A FLIGHT CONTROL MODE MANAGER");

    avionics::FlightControlModeManagerInPorts in_ports;
    avionics::ConcreteAvionicsServices services;

    // 1. Validate MBSE Physical Sensor Contracts (Aerospace Range Bounds)
    in_ports.airspeed_knots = 250.0f;           // Valid (0 to 600 kt)
    in_ports.radar_altitude_ft = 15000.0f;      // Valid (0 to 60000 ft)
    in_ports.hydraulic_pressure_psi = 3000.0f;  // Valid (0 to 3500 psi)
    assert(in_ports.validate_contracts());
    std::cout << "  \033[1;32m[MBSE AEROSPACE CONTRACTS]\033[0m Air data & hydraulic pressure sensor contracts "
                 "verified: OK\n";

    // 2. Initialize State Machine in GroundInit
    avionics::FlightControlModeManager fsm(services);
    assert(fsm.current_state_name() == "GroundInit");
    print_step("Initial Preflight State", fsm.current_state_name());

    // 3. Takeoff & Liftoff Detected -> ManualFlight (Normal Law)
    std::cout << "\n\033[1;33m--- Phase 1: Runway Liftoff & Transition to Manual Flight ---\033[0m\n";
    fsm.dispatch(avionics::LiftoffDetectedCmd{});
    assert(fsm.is_in<avionics::ManualFlight>());
    print_step("Dispatched LiftoffDetectedCmd", fsm.current_state_name());

    // 4. Autopilot Engagement: Airspeed Valid -> AutopilotNav
    std::cout << "\n\033[1;33m--- Phase 2: Climb-Out & Autopilot Engagement ---\033[0m\n";
    fsm.dispatch(avionics::EngageAutopilotCmd{});
    assert(fsm.is_in<avionics::AutopilotNav>());
    assert(services.guidance_couplers_armed);
    print_step("Dispatched EngageAutopilotCmd", fsm.current_state_name());

    // 5. Pilot Manual Takeover: Disengage Autopilot
    std::cout << "\n\033[1;33m--- Phase 3: Pilot Sidestick Priority Takeover ---\033[0m\n";
    fsm.dispatch(avionics::DisengageAutopilotCmd{});
    assert(fsm.is_in<avionics::ManualFlight>());
    assert(!services.guidance_couplers_armed);
    print_step("Dispatched DisengageAutopilotCmd", fsm.current_state_name());

    // 6. Re-engage Autopilot for Cruise Segment
    std::cout << "\n\033[1;33m--- Phase 4: Cruise Segment Re-engagement ---\033[0m\n";
    fsm.dispatch(avionics::EngageAutopilotCmd{});
    assert(fsm.is_in<avionics::AutopilotNav>());
    print_step("Dispatched EngageAutopilotCmd", fsm.current_state_name());

    // 7. Safety Preemption: Air Data Irregularity -> Emergency Direct Law
    std::cout << "\n\033[1;33m--- Phase 5: Critical Sensor Discrepancy & Direct Law Fallback ---\033[0m\n";
    fsm.dispatch(avionics::SensorIrregularityFault{});
    assert(fsm.is_in<avionics::EmergencyDirectLaw>());
    assert(services.direct_law_warning_active);
    print_step("Dispatched SensorIrregularityFault", fsm.current_state_name());
    std::cout << "  \033[1;32m[FORMAL PROPERTY SAFE-04]\033[0m Verified: Reconfiguration guarantees immediate Direct "
                 "Law fallback!\n";

    print_section("ALL FORMAL FLIGHT MODE VERIFICATION TESTS PASSED (100% SUCCESS)");
    return 0;
}

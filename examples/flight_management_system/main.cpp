/**
 * @file main.cpp
 * @brief Reference Avionics Flight Management System (FMS) Case Study.
 *
 * Demonstrates:
 * - Deterministic, High-Integrity Safety-Oriented Architecture
 * - Zero dynamic heap allocations across all execution phases
 * - SysML v2 Strongly-Typed Enums and Structured Data
 * - Circular Black-Box Flight Recorder (Audit Trail)
 * - Deterministic Tick-Based Timer Stepping
 * - Synchronous Input/Output Port and Register Contracts
 */

#include <cassert>
#include <iomanip>
#include <iostream>

#include "fms_fsm.hpp"

int main() {
    std::cout << "============================================================================\n";
    std::cout << " Avionics Flight Management System (FMS) Reference Case Study\n";
    std::cout << " Architecture: Deterministic High-Integrity / Zero Dynamic Heap Allocations\n";
    std::cout << "============================================================================\n\n";

    // 1. Setup Partitioned Domain Structures (Stack-Allocated, Zero Heap)
    avionics::FlightManagementSystemInPorts in_ports{};
    in_ports.cross_track_error_m = 0.0f;
    in_ports.vertical_deviation_ft = 0.0f;
    in_ports.baro_alt_ft = 0.0f;

    avionics::FlightManagementSystemOutPorts out_ports{};
    avionics::FlightManagementSystemRegisters registers{};
    registers.waypoints_sequenced = 0;
    registers.nav_cycles = 0;

    // 2. Instantiate Synchronous FMS Core with Flight Recorder Observer (Stack-Allocated)
    using FmsWithRecorder = fsm::make_fsm<
        avionics::FlightManagementSystemTable,
        fsm::with_initial_state<avionics::PreflightState>,
        fsm::with_ports<avionics::FlightManagementSystemInPorts, avionics::FlightManagementSystemOutPorts>,
        fsm::with_registers<avionics::FlightManagementSystemRegisters>,
        fsm::with_services<avionics::FlightManagementSystemServices>,
        fsm::with_trace_buffer<64>,
        fsm::with_deferred_capacity<16>,
        fsm::with_timer_capacity<8>
    >;

    FmsWithRecorder fms_core;
    fms_core.set_registers(registers);

    std::cout << "[FMS INIT] System initialized on ground.\n";
    std::cout << "  Active State: " << fms_core.current_state_name() << "\n";
    assert(fms_core.is_in<avionics::PreflightState>());

    // 4. Phase 1: Preflight -> Taxi
    std::cout << "\n[FMS PHASE 1] Preflight to Taxi Clearance\n";
    auto r1 = fms_core.dispatch(avionics::StartTaxi{}, in_ports, out_ports);
    assert(r1.is_success());
    assert(fms_core.is_in<avionics::TaxiState>());
    assert(out_ports.flight_director_active == true);
    std::cout << "  Event: StartTaxi -> Active State: " << fms_core.current_state_name()
              << " | Flight Director: " << (out_ports.flight_director_active ? "ON" : "OFF") << "\n";
    fms_core.observer().advance_tick(1);

    // 5. Phase 2: Taxi -> Airborne (Climb)
    std::cout << "\n[FMS PHASE 2] Takeoff Roll & Initial Climb\n";
    auto r2 = fms_core.dispatch(avionics::TakeoffClearance{}, in_ports, out_ports);
    assert(r2.is_success());
    assert(fms_core.is_in<avionics::ClimbState>());
    assert(out_ports.autothrottle_engaged == true);
    std::cout << "  Event: TakeoffClearance -> Active State: " << fms_core.current_state_name()
              << " | Autothrottle: " << (out_ports.autothrottle_engaged ? "ENGAGED" : "DISENGAGED") << "\n";
    fms_core.observer().advance_tick(1);

    // 6. Phase 3: Climb -> Cruise (Waypoints Sequenced)
    std::cout << "\n[FMS PHASE 3] En-Route Cruise Waypoint Navigation\n";
    in_ports.baro_alt_ft = 35000.0f;
    auto r3 = fms_core.dispatch(avionics::CruiseAltReached{}, in_ports, out_ports);
    assert(r3.is_success());
    assert(fms_core.is_in<avionics::CruiseState>());
    std::cout << "  Event: CruiseAltReached -> Active State: " << fms_core.current_state_name()
              << " | Waypoints Sequenced: " << fms_core.registers().waypoints_sequenced << "\n";
    assert(fms_core.registers().waypoints_sequenced == 1);
    fms_core.observer().advance_tick(1);

    // 7. Phase 4: Cruise -> Descent (Top of Descent reached)
    std::cout << "\n[FMS PHASE 4] Descent Management\n";
    in_ports.baro_alt_ft = 12000.0f;
    auto r4 = fms_core.dispatch(avionics::TopOfDescent{}, in_ports, out_ports);
    assert(r4.is_success());
    assert(fms_core.is_in<avionics::DescentState>());
    std::cout << "  Event: TopOfDescent -> Active State: " << fms_core.current_state_name() << "\n";
    fms_core.observer().advance_tick(1);

    // 8. Phase 5: Descent -> Terminal Approach
    std::cout << "\n[FMS PHASE 5] Terminal ILS Approach Capture\n";
    in_ports.cross_track_error_m = 2.5f;
    in_ports.vertical_deviation_ft = -10.0f;
    auto r5 = fms_core.dispatch(avionics::LocalizerCaptured{}, in_ports, out_ports);
    assert(r5.is_success());
    assert(fms_core.is_in<avionics::ApproachState>());
    std::cout << "  Event: LocalizerCaptured -> Active State: " << fms_core.current_state_name()
              << " | CrossTrack: " << in_ports.cross_track_error_m << "m\n";
    fms_core.observer().advance_tick(1);

    // 9. Phase 6: Touchdown -> Landed
    std::cout << "\n[FMS PHASE 6] Runway Touchdown & Rollout\n";
    auto r6 = fms_core.dispatch(avionics::Touchdown{}, in_ports, out_ports);
    assert(r6.is_success());
    assert(fms_core.is_in<avionics::Landed>());
    assert(out_ports.autothrottle_engaged == false);
    assert(out_ports.flight_director_active == false);
    std::cout << "  Event: Touchdown -> Active State: " << fms_core.current_state_name()
              << " | Autothrottle: " << (out_ports.autothrottle_engaged ? "ON" : "OFF")
              << " | Flight Director: " << (out_ports.flight_director_active ? "ON" : "OFF") << "\n";
    fms_core.observer().advance_tick(1);

    // 10. Advance Deterministic Real-Time Clock
    std::cout << "\n[FMS HARD REAL-TIME] Deterministic Timer Stepping (Synchronous Tick)\n";
    fms_core.timer_manager().start_timer(101, 200, false);
    std::size_t exp_tick1 = fms_core.tick(100);
    std::cout << "  Tick 100ms: Expired Timers = " << exp_tick1 << " (Expected: 0)\n";
    assert(exp_tick1 == 0);

    std::size_t exp_tick2 = fms_core.tick(150);
    std::cout << "  Tick 150ms: Expired Timers = " << exp_tick2 << " (Expected: 1)\n";
    assert(exp_tick2 == 1);

    // 11. Dump Flight Recorder Audit Trail
    std::cout << "\n[SAFETY AUDIT TRAIL] Flight Recorder Black-Box Trace Dump:\n";
    fms_core.observer().dump(std::cout);

    std::cout << "\n[SUCCESS] Flight Management System Case Study executed successfully!\n";
    return 0;
}

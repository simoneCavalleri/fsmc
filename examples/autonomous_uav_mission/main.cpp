#include <cassert>
#include <iomanip>
#include <iostream>

#include "uav_mission_fsm.hpp"

int main() {
    std::cout << "============================================================================\n";
    std::cout << " Autonomous UAV Mission & Flight Executive (Full FsmIr Showcase)\n";
    std::cout << " Demonstrating: EFSM Variables, Typed Payloads, History [H], and LTL Properties\n";
    std::cout << "============================================================================\n\n";

    // 1. Instantiate State Machine with Auto-Generated EFSM Context
    uav::UavMissionFSMContext ctx;
    ctx.battery_percent = 95;
    ctx.altitude_m = 0;
    ctx.waypoints_completed = 0;
    ctx.retry_count = 0;

    uav::UavMissionFSM fsm(ctx);

    std::cout << "[PHASE 1] Preflight Initialization & Sensor Calibration\n";
    std::cout << "  Current State: " << fsm.current_state_name() << "\n";
    assert(fsm.is_in<uav::SensorCalib>());

    // 2. Dispatch calibration completion
    fsm.dispatch(uav::CalibrationOk{});
    std::cout << "  Dispatched: CalibrationOk -> Current State: " << fsm.current_state_name() << "\n";
    assert(fsm.is_in<uav::SystemReady>());

    // 3. Test strongly-typed signal with payload & validator
    uav::EvTelemetry telemetry(11850, 0, true);
    std::cout << "  Telemetry Packet: Battery " << telemetry.battery_mv << " mV, Alt " << telemetry.altitude_cm
              << " cm, Valid: " << (telemetry.is_valid() ? "YES" : "NO") << "\n";
    assert(telemetry.is_valid());

    uav::EvWaypointCmd wp_cmd(45.4642, 9.1900, 120.0f);
    std::cout << "  Waypoint Command: Lat " << wp_cmd.lat << ", Lon " << wp_cmd.lon << ", Alt " << wp_cmd.target_alt
              << " m, Valid: " << (wp_cmd.is_valid() ? "YES" : "NO") << "\n";
    assert(wp_cmd.is_valid());

    // 4. Takeoff and Climb to cruise altitude
    std::cout << "\n[PHASE 2] Takeoff & Climb to Target Altitude\n";
    fsm.dispatch(uav::TakeoffCmd{});
    std::cout << "  Dispatched: TakeoffCmd -> Current State: " << fsm.current_state_name() << "\n";
    assert(fsm.is_in<uav::Ascending>());

    ctx.altitude_m = 120;
    fsm.dispatch(uav::TargetAltitudeReached{});
    std::cout << "  Dispatched: TargetAltitudeReached -> Current State: " << fsm.current_state_name()
              << " (Altitude: " << ctx.altitude_m << "m)\n";
    assert(fsm.is_in<uav::WaypointNav>());

    // 5. Autonomous Navigation & Search Pattern Execution
    std::cout << "\n[PHASE 3] Autonomous Navigation & Search Pattern Execution\n";
    ctx.waypoints_completed += 1;
    fsm.dispatch(uav::AreaReached{});
    std::cout << "  Dispatched: AreaReached -> Current State: " << fsm.current_state_name()
              << " (Waypoints: " << ctx.waypoints_completed << ")\n";
    assert(fsm.is_in<uav::SearchPattern>());

    // 6. Test Deferred Events in Navigating Composite State
    std::cout << "\n[PHASE 4] Deferred Event Handling During Active Flight\n";
    auto defer_res = fsm.dispatch(uav::EvTelemetryPing{});
    std::cout << "  Dispatched EvTelemetryPing during SearchPattern: "
              << (defer_res.is_deferred() ? "DEFERRED [OK]" : "HANDLED") << "\n";

    // 7. History Restoration [H] After Temporary Loiter/Pause
    std::cout << "\n[PHASE 5] History Restoration [H] After Temporary Loiter\n";
    std::cout << "  Triggering temporary hover hold...\n";
    fsm.dispatch(uav::PauseCmd{});
    std::cout << "  Dispatched: PauseCmd -> Current State: " << fsm.current_state_name() << "\n";
    assert(fsm.is_in<uav::HoverPause>());

    std::cout << "  Resuming mission. Restoring historical sub-state [H] (expecting SearchPattern)...\n";
    fsm.dispatch(uav::ResumeMissionCmd{});
    std::cout << "  Dispatched: ResumeMissionCmd -> Current State: " << fsm.current_state_name() << "\n";
    assert(fsm.is_in<uav::SearchPattern>());

    // 8. Emergency Low Battery FailSafe & Autonomous Landing
    std::cout << "\n[PHASE 6] Emergency Low Battery FailSafe & Autonomous Landing\n";
    ctx.battery_percent = 12;
    fsm.dispatch(uav::LowBatteryEvent{});
    std::cout << "  Dispatched: LowBatteryEvent -> Current State: " << fsm.current_state_name() << "\n";
    assert(fsm.is_in<uav::ReturnToHome>());

    ctx.altitude_m = 10;
    fsm.dispatch(uav::HomePointReached{});
    std::cout << "  Dispatched: HomePointReached -> Current State: " << fsm.current_state_name()
              << " (Altitude: " << ctx.altitude_m << "m)\n";
    assert(fsm.is_in<uav::SafeLanding>());

    ctx.altitude_m = 0;
    fsm.dispatch(uav::TouchdownEvent{});
    std::cout << "  Dispatched: TouchdownEvent -> Current State: " << fsm.current_state_name()
              << " (Altitude: " << ctx.altitude_m << "m)\n";
    assert(fsm.is_in<uav::Landed>());

    fsm.dispatch(uav::ShutdownCmd{});
    std::cout << "  Dispatched: ShutdownCmd -> Current State: " << fsm.current_state_name() << "\n";
    assert(fsm.is_in<uav::FinalShutdown>());

    std::cout << "\n============================================================================\n";
    std::cout << " [SUCCESS] Flagship UAV Mission Simulation Completed with Zero Errors!\n";
    std::cout << " Final EFSM Context:\n";
    std::cout << "   - Battery Remaining       : " << ctx.battery_percent << "%\n";
    std::cout << "   - Final Altitude          : " << ctx.altitude_m << "m\n";
    std::cout << "   - Waypoints Visited       : " << ctx.waypoints_completed << "\n";
    std::cout << "============================================================================\n";

    return 0;
}

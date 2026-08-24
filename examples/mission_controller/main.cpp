#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <thread>

// Forward declaration of custom context in namespace mission
namespace mission {
struct MissionContext {
    bool flight_clearance_granted = true;
    bool solar_panels_deployed = false;
    bool thrusters_armed = false;
    int altitude_km = 0;
    int telemetry_pings_received = 0;
    int clock_sync_count = 0;
    int alarm_triggered_count = 0;
    float battery_charge_pct = 100.0F;
};
}  // namespace mission

#include "mission_fsm.hpp"

namespace mission {

// ============================================================================
// Custom Guard Functors
// ============================================================================
struct ValidClearanceGuard {
    [[nodiscard]] constexpr bool operator()(const AuthorizeCmd& /*evt*/, const auto& /*src*/,
                                            const MissionContext& ctx) const noexcept {
        return ctx.flight_clearance_granted;
    }
};

struct NoClearanceGuard {
    [[nodiscard]] constexpr bool operator()(const AuthorizeCmd& /*evt*/, const auto& /*src*/,
                                            const MissionContext& ctx) const noexcept {
        return !ctx.flight_clearance_granted;
    }
};

// ============================================================================
// Custom Action Functors
// ============================================================================
struct LogCalibrationAction {
    constexpr void operator()(const CalibrationOkEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              MissionContext& /*ctx*/) const {
        std::cout << "\033[1;32m  [ACTION]\033[0m Pre-flight telemetry calibrated -> Gyroscopes, Star Tracker & IMU "
                     "nominal.\n";
    }
};

struct ArmEnginesAction {
    constexpr void operator()(const AuthorizeCmd& /*evt*/, auto& /*src*/, auto& /*dst*/, MissionContext& ctx) const {
        ctx.thrusters_armed = true;
        std::cout
            << "\033[1;32m  [ACTION]\033[0m Flight clearance VERIFIED via <<choice>> -> Main propulsion ARMED for "
               "Ascent!\n";
    }
};

struct TriggerAlarmAction {
    constexpr void operator()(const AuthorizeCmd& /*evt*/, auto& /*src*/, auto& /*dst*/, MissionContext& ctx) const {
        ctx.alarm_triggered_count++;
        std::cout << "\033[1;31m  [ACTION/SAFETY]\033[0m Clearance REJECTED via <<choice>> -> Flight aborted, safe "
                     "hold engaged!\n";
    }
};

struct DeploySolarPanelsAction {
    constexpr void operator()(const AltitudeReachedEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              MissionContext& ctx) const {
        ctx.altitude_km = 420;
        ctx.solar_panels_deployed = true;
        std::cout
            << "\033[1;32m  [ACTION]\033[0m Altitude 420 km reached -> Solar array wings DEPLOYED and sun-tracking "
               "engaged.\n";
    }
};

struct StabilizeAttitudeAction {
    constexpr void operator()(const OrbitInsertedEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              MissionContext& /*ctx*/) const {
        std::cout
            << "\033[1;32m  [ACTION]\033[0m Circular orbit insertion complete (LEO 420km) -> Attitude stabilized.\n";
    }
};

struct LogTelemetryAction {
    constexpr void operator()(const PingTelemetry& /*evt*/, auto& /*src*/, auto& /*dst*/, MissionContext& ctx) const {
        ctx.telemetry_pings_received++;
        std::cout << "\033[1;36m  [ACTION/INTERNAL]\033[0m Telemetry heartbeat #" << ctx.telemetry_pings_received
                  << " acknowledged (Zero state exit/entry cost)\n";
    }
};

struct BufferCommandsAction {
    constexpr void operator()(const LinkDegradedEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              MissionContext& /*ctx*/) const {
        std::cout
            << "\033[1;33m  [ACTION]\033[0m RF telemetry link degraded -> Outbound packets buffered in flash memory.\n";
    }
};

struct SyncClockAction {
    constexpr void operator()(const LinkRestoredEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              MissionContext& ctx) const {
        ctx.clock_sync_count++;
        std::cout
            << "\033[1;32m  [ACTION]\033[0m RF ground link re-acquired -> Master spacecraft clock resynchronized.\n";
    }
};

struct RetractPanelsAction {
    constexpr void operator()(const ReturnHomeCmd& /*evt*/, auto& /*src*/, auto& /*dst*/, MissionContext& ctx) const {
        ctx.solar_panels_deployed = false;
        std::cout << "\033[1;33m  [ACTION]\033[0m Retrograde burn initiated -> Solar arrays retracted for atmospheric "
                     "re-entry.\n";
    }
};

struct ShutdownSystemsAction {
    constexpr void operator()(const TouchdownEvent& /*evt*/, auto& /*src*/, auto& /*dst*/, MissionContext& ctx) const {
        ctx.thrusters_armed = false;
        std::cout << "\033[1;32m  [ACTION]\033[0m Touchdown confirmed on recovery pad -> Main engines locked in safe "
                     "state.\n";
    }
};

}  // namespace mission

namespace {

void print_header(std::string_view title) {
    std::cout << "\n\033[1;35m======================================================================\033[0m\n";
    std::cout << "\033[1;37m  " << title << "\033[0m\n";
    std::cout << "\033[1;35m======================================================================\033[0m\n";
}

void print_spacecraft_hud(const mission::MissionContext& ctx, std::string_view state_name) {
    std::cout << "  ┌─────────────────────────────────────────────────────────────┐\n";
    std::cout << "  │ \033[1mFSM State:\033[0m " << std::left << std::setw(15) << state_name
              << " │ \033[1mAltitude:\033[0m " << std::left << std::setw(6) << (std::to_string(ctx.altitude_km) + " km")
              << " │ \033[1mPanels:\033[0m "
              << (ctx.solar_panels_deployed ? "\033[32mDEPLOYED\033[0m" : "\033[33mSTOWED\033[0m  ") << " │\n";
    std::cout << "  │ \033[1mThrusters:\033[0m " << std::left << std::setw(15)
              << (ctx.thrusters_armed ? "\033[32mARMED\033[0m" : "\033[31mSAFE\033[0m") << " │ \033[1mPings:\033[0m    "
              << std::left << std::setw(6) << ctx.telemetry_pings_received << " │ \033[1mBattery:\033[0m " << std::fixed
              << std::setprecision(0) << ctx.battery_charge_pct << " %     │\n";
    std::cout << "  └─────────────────────────────────────────────────────────────┘\n";
}

}  // namespace

int main() {
    print_header("fsmc Aerospace: Autonomous Spacecraft Mission Controller");

    mission::MissionContext context;
    context.flight_clearance_granted = true;

    // 1. Instantiate the State Machine with custom Context
    mission::MissionFSM fsm(context);

    // Attach live telemetry observer
    fsm.set_observer([](const fsm::transition_info& info) {
        std::cout << "\033[1;34m[MISSION OBSERVER]\033[0m " << info.source << " --(\033[1m" << info.event
                  << "\033[0m)--> " << info.target << (info.is_internal() ? " \033[36m[INTERNAL]\033[0m" : "") << "\n";
    });

    // Phase 1: Pre-Flight Systems Initialization
    print_header("PHASE 1: Pre-Flight Systems Initialization (Composite Standby)");
    print_spacecraft_hud(fsm.context(), fsm.current_state_name());
    assert(fsm.is_in_state<mission::Diagnostics>());

    std::cout << "\n--> [Telemetry] Verifying sensor suite calibration...\n";
    auto res = fsm.dispatch(mission::CalibrationOkEvent{});
    assert(res.is_success());
    assert(fsm.is_in_state<mission::Calibrated>());
    print_spacecraft_hud(fsm.context(), fsm.current_state_name());

    // Phase 2: Flight Authorization via UML Choice Pseudostate
    print_header("PHASE 2: Flight Authorization via <<choice>> Pseudostate");
    std::cout << "\n--> [Telemetry] Requesting launch authorization from Range Safety...\n";
    res = fsm.dispatch(mission::AuthorizeCmd{});
    assert(res.is_success());
    assert(fsm.is_in_state<mission::Ascending>());
    assert(context.thrusters_armed);
    print_spacecraft_hud(fsm.context(), fsm.current_state_name());

    // Alternative Choice Branch Verification (Denial Path)
    std::cout << "\n  \033[33m[Verification]\033[0m Simulating alternative choice branch when clearance is denied:\n";
    {
        mission::MissionContext denied_ctx;
        denied_ctx.flight_clearance_granted = false;
        mission::MissionFSM abort_fsm(denied_ctx);
        abort_fsm.dispatch(mission::CalibrationOkEvent{});
        auto abort_res = abort_fsm.dispatch(mission::AuthorizeCmd{});
        assert(abort_res.is_success());
        assert(abort_fsm.is_in_state<mission::Aborted>());
        assert(denied_ctx.alarm_triggered_count == 1);
        std::cout << "  (Choice pseudostate deterministically routed to Aborted state: "
                  << abort_fsm.current_state_name() << ")\n";
    }

    // Phase 3: Ascent, Staging & Circular Orbit Insertion
    print_header("PHASE 3: Ascent, Fairing Separation & Orbit Insertion");
    std::cout << "\n--> [Telemetry] Spacecraft reaches orbital altitude (420 km)...\n";
    res = fsm.dispatch(mission::AltitudeReachedEvent{});
    assert(res.is_success());
    assert(fsm.is_in_state<mission::Cruising>());
    print_spacecraft_hud(fsm.context(), fsm.current_state_name());

    std::cout << "\n--> [Telemetry] Main engine cutoff & circular orbit insertion burn complete...\n";
    res = fsm.dispatch(mission::OrbitInsertedEvent{});
    assert(res.is_success());
    assert(fsm.is_in_state<mission::Orbiting>());
    print_spacecraft_hud(fsm.context(), fsm.current_state_name());

    // Phase 4: Zero-Overhead Internal Transitions
    print_header("PHASE 4: In-Orbit Telemetry Heartbeats (Internal Transitions)");
    for (int i = 0; i < 3; ++i) {
        res = fsm.dispatch(mission::PingTelemetry{});
        assert(res.is_success());
    }
    assert(context.telemetry_pings_received == 3);
    assert(fsm.is_in_state<mission::Orbiting>());
    print_spacecraft_hud(fsm.context(), fsm.current_state_name());

    // Phase 5: RF Link Degradation & Ground Station Acquisition
    print_header("PHASE 5: Deep-Space Communication Degradation & Resynchronization");
    std::cout << "\n--> [Telemetry] Spacecraft enters ground station shadow (RF link lost)...\n";
    res = fsm.dispatch(mission::LinkDegradedEvent{});
    assert(res.is_success());
    assert(fsm.is_in_state<mission::SignalLost>());

    std::cout << "\n--> [Telemetry] Ground station signal re-acquired (Lock established)...\n";
    res = fsm.dispatch(mission::LinkRestoredEvent{});
    assert(res.is_success());
    assert(fsm.is_in_state<mission::Orbiting>());
    assert(context.clock_sync_count == 1);
    std::cout << "  (State seamlessly restored to Orbiting!)\n";

    // Phase 6: De-orbit, Re-entry & Recovery Touchdown
    print_header("PHASE 6: De-Orbit, Re-entry Burn & Recovery Touchdown");
    std::cout << "\n--> [Telemetry] Dispatching ReturnHomeCmd (De-orbit sequence)...\n";
    res = fsm.dispatch(mission::ReturnHomeCmd{});
    assert(res.is_success());
    assert(fsm.is_in_state<mission::Landing>());
    print_spacecraft_hud(fsm.context(), fsm.current_state_name());

    std::cout << "\n--> [Telemetry] Drogue & main parachutes deployed -> Touchdown!\n";
    res = fsm.dispatch(mission::TouchdownEvent{});
    assert(res.is_success());
    assert(fsm.is_in_state<mission::MissionCompleted>());
    print_spacecraft_hud(fsm.context(), fsm.current_state_name());

    // Phase 7: Multithreaded Asynchronous Worker Execution
    print_header("PHASE 7: Asynchronous Background Thread Execution");
    mission::MissionContext async_ctx;
    mission::ThreadSafeMissionFSM async_fsm(async_ctx);
    async_fsm.start_worker();

    std::cout << "  Posting events asynchronously to background thread...\n";
    async_fsm.post(mission::CalibrationOkEvent{});
    async_fsm.post(mission::AuthorizeCmd{});
    async_fsm.post(mission::AltitudeReachedEvent{});

    while (!async_fsm.is_in_state<mission::Cruising>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cout << "  \033[32m[SUCCESS]\033[0m Async worker successfully transitioned to Cruising!\n";
    async_fsm.stop_worker();

    std::cout << "\n\033[1;32m======================================================================\033[0m\n";
    std::cout << "\033[1;32m  [SUCCESS] All OMG UML 2.5 & SysML capabilities demonstrated!        \033[0m\n";
    std::cout << "\033[1;32m======================================================================\033[0m\n";
    return 0;
}

#include <cassert>
#include <chrono>
#include <iostream>
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
        std::cout << "  [ACTION] Diagnostics complete -> Gyroscopes & IMU calibrated.\n";
    }
};

struct ArmEnginesAction {
    constexpr void operator()(const AuthorizeCmd& /*evt*/, auto& /*src*/, auto& /*dst*/, MissionContext& ctx) const {
        ctx.thrusters_armed = true;
        std::cout << "  [ACTION] Clearance VERIFIED via <<choice>> -> Main thrusters ARMED for Ascent!\n";
    }
};

struct TriggerAlarmAction {
    constexpr void operator()(const AuthorizeCmd& /*evt*/, auto& /*src*/, auto& /*dst*/, MissionContext& ctx) const {
        ctx.alarm_triggered_count++;
        std::cout << "  [ACTION] Clearance REJECTED via <<choice>> -> Alarm triggered, mission ABORTED!\n";
    }
};

struct DeploySolarPanelsAction {
    constexpr void operator()(const AltitudeReachedEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              MissionContext& ctx) const {
        ctx.altitude_km = 400;
        ctx.solar_panels_deployed = true;
        std::cout << "  [ACTION] Altitude 400km reached -> Solar panels DEPLOYED.\n";
    }
};

struct StabilizeAttitudeAction {
    constexpr void operator()(const OrbitInsertedEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              MissionContext& /*ctx*/) const {
        std::cout << "  [ACTION] Orbit insertion complete -> Attitude stabilized.\n";
    }
};

struct LogTelemetryAction {
    constexpr void operator()(const PingTelemetry& /*evt*/, auto& /*src*/, auto& /*dst*/, MissionContext& ctx) const {
        ctx.telemetry_pings_received++;
        std::cout << "  [ACTION/INTERNAL] Telemetry Ping #" << ctx.telemetry_pings_received
                  << " acknowledged (Zero exit/entry overhead).\n";
    }
};

struct BufferCommandsAction {
    constexpr void operator()(const LinkDegradedEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              MissionContext& /*ctx*/) const {
        std::cout << "  [ACTION] RF link degraded -> Command buffer enabled.\n";
    }
};

struct SyncClockAction {
    constexpr void operator()(const LinkRestoredEvent& /*evt*/, auto& /*src*/, auto& /*dst*/,
                              MissionContext& ctx) const {
        ctx.clock_sync_count++;
        std::cout << "  [ACTION] RF link restored -> Onboard clock resynchronized.\n";
    }
};

struct RetractPanelsAction {
    constexpr void operator()(const ReturnHomeCmd& /*evt*/, auto& /*src*/, auto& /*dst*/, MissionContext& ctx) const {
        ctx.solar_panels_deployed = false;
        std::cout << "  [ACTION] De-orbit burn initiated -> Solar panels retracted.\n";
    }
};

struct ShutdownSystemsAction {
    constexpr void operator()(const TouchdownEvent& /*evt*/, auto& /*src*/, auto& /*dst*/, MissionContext& ctx) const {
        ctx.thrusters_armed = false;
        std::cout << "  [ACTION] Touchdown confirmed -> All spacecraft systems safe.\n";
    }
};

}  // namespace mission

// ============================================================================
// Main Showcase Execution
// ============================================================================
int main() {
    std::cout << "======================================================================\n"
              << "       FSMC SHOWCASE: AUTOMATED MISSION CONTROLLER (OMG UML 2.5)      \n"
              << "======================================================================\n\n";

    mission::MissionContext context;
    context.flight_clearance_granted = true;

    // 1. Instantiate the State Machine with custom Context
    mission::MissionFSM fsm(context);

    std::cout << "[PHASE 1] Initial State & Context Verification\n";
    std::cout << "  Current State: " << fsm.current_state_name() << "\n";
    assert(fsm.is_in_state<mission::Diagnostics>());
    assert(fsm.current_state_name() == "Diagnostics");

    // 2. Hierarchical Sub-State Diagnostics
    std::cout << "\n[PHASE 2] Composite State Diagnostics (Standby -> Diagnostics)\n";
    bool handled = fsm.dispatch(mission::CalibrationOkEvent{});
    (void)handled;
    assert(handled);
    assert(fsm.is_in_state<mission::Calibrated>());
    std::cout << "  Current State: " << fsm.current_state_name() << "\n";

    // 3A. Choice Pseudostate Clearance Evaluation (Nominal Path: Granted -> Ascending)
    std::cout << "\n[PHASE 3A] Flight Authorization via <<choice>> (Clearance Granted -> Ascending)\n";
    handled = fsm.dispatch(mission::AuthorizeCmd{});
    assert(handled);
    assert(fsm.is_in_state<mission::Ascending>());
    assert(context.thrusters_armed);
    std::cout << "  Current State: " << fsm.current_state_name() << "\n";

    // 3B. Alternative Choice Branch Demonstration (Clearance Denied -> Aborted)
    std::cout << "\n[PHASE 3B] Choice Pseudostate Alternative Branch (Clearance Denied -> Aborted)\n";
    {
        mission::MissionContext denied_context;
        denied_context.flight_clearance_granted = false;
        mission::MissionFSM abort_fsm(denied_context);

        abort_fsm.dispatch(mission::CalibrationOkEvent{});
        assert(abort_fsm.is_in_state<mission::Calibrated>());

        std::cout << "  Dispatching AuthorizeCmd with flight_clearance_granted = false...\n";
        bool abort_handled = abort_fsm.dispatch(mission::AuthorizeCmd{});
        (void)abort_handled;
        assert(abort_handled);
        assert(abort_fsm.is_in_state<mission::Aborted>());
        assert(denied_context.alarm_triggered_count == 1);
        std::cout << "  Alternative branch successfully transitioned to: " << abort_fsm.current_state_name() << "\n";
    }

    // 4. Hierarchical Flight Ascent -> Cruising -> Orbiting
    std::cout << "\n[PHASE 4] Orbital Ascent Sequence (Ascending -> Cruising -> Orbiting)\n";
    handled = fsm.dispatch(mission::AltitudeReachedEvent{});
    assert(handled);
    assert(fsm.is_in_state<mission::Cruising>());
    assert(context.solar_panels_deployed);
    std::cout << "  Current State: " << fsm.current_state_name() << "\n";

    handled = fsm.dispatch(mission::OrbitInsertedEvent{});
    assert(handled);
    assert(fsm.is_in_state<mission::Orbiting>());
    std::cout << "  Current State: " << fsm.current_state_name() << "\n";

    // 5. Internal Transitions (Zero Exit/Entry Overhead)
    std::cout << "\n[PHASE 5] High-Frequency Internal Telemetry Pings\n";
    for (int i = 0; i < 3; ++i) {
        handled = fsm.dispatch(mission::PingTelemetry{});
        assert(handled);
    }
    assert(context.telemetry_pings_received == 3);
    assert(fsm.is_in_state<mission::Orbiting>());

    // 6. Communication Blackout & History Recovery
    std::cout << "\n[PHASE 6] Deep-Space Blackout & RF Recovery\n";
    handled = fsm.dispatch(mission::LinkDegradedEvent{});
    assert(handled);
    assert(fsm.is_in_state<mission::SignalLost>());
    std::cout << "  Current State: " << fsm.current_state_name() << " (Telemetry buffered)\n";

    std::cout << "  Restoring RF connection...\n";
    handled = fsm.dispatch(mission::LinkRestoredEvent{});
    assert(handled);
    assert(fsm.is_in_state<mission::Orbiting>());
    assert(context.clock_sync_count == 1);
    std::cout << "  Current State: " << fsm.current_state_name() << " (Resumed active state!)\n";

    // 7. Unhandled Event Invariance Verification
    std::cout << "\n[PHASE 7] Deterministic Unhandled Event Handling\n";
    handled = fsm.dispatch(mission::CalibrationOkEvent{});  // Calibration is not accepted during Orbiting
    assert(!handled);
    assert(fsm.is_in_state<mission::Orbiting>());
    std::cout << "  Spurious event safely ignored (dispatch returned false, state preserved: "
              << fsm.current_state_name() << ").\n";

    // 8. De-orbit & Landing
    std::cout << "\n[PHASE 8] De-Orbit and Recovery\n";
    handled = fsm.dispatch(mission::ReturnHomeCmd{});
    assert(handled);
    assert(fsm.is_in_state<mission::Landing>());
    std::cout << "  Current State: " << fsm.current_state_name() << "\n";

    handled = fsm.dispatch(mission::TouchdownEvent{});
    assert(handled);
    assert(fsm.is_in_state<mission::MissionCompleted>());
    std::cout << "  Current State: " << fsm.current_state_name() << "\n";

    // 9. Asynchronous Thread-Safe Worker Showcase
    std::cout << "\n[PHASE 9] Asynchronous Multithreaded Worker Execution\n";
    mission::MissionContext async_ctx;
    mission::ThreadSafeMissionFSM async_fsm(async_ctx);
    async_fsm.start_worker();

    std::cout << "  Posting asynchronous events to background worker thread...\n";
    async_fsm.post(mission::CalibrationOkEvent{});
    async_fsm.post(mission::AuthorizeCmd{});
    async_fsm.post(mission::AltitudeReachedEvent{});

    while (!async_fsm.is_in_state<mission::Cruising>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "  Async FSM successfully reached Cruising state under background thread.\n";
    async_fsm.stop_worker();

    std::cout << "\n======================================================================\n"
              << "  [SUCCESS] All OMG UML 2.5 capabilities demonstrated successfully!  \n"
              << "======================================================================\n";
    return 0;
}

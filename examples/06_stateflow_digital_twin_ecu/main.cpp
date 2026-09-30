/**
 * @file main.cpp
 * @brief Industrial Case Study 06: Stateflow Digital Twin ECU with Chained Junctions & Time-Travel Rollback.
 */

#include <cassert>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

#ifndef VERIFY_EXPR
#define VERIFY_EXPR(expr) \
    do {                  \
        if (!(expr)) {    \
            std::abort(); \
        }                 \
    } while (false)
#endif

#include "ecu_fsm.hpp"

namespace automotive::ecu {

// ============================================================================
// Concrete Services & Port Implementation
// ============================================================================
struct EcuServices : public EcuEngineFSMServices {
    int actions_logged = 0;
    bool sensors_ok_val = true;
    bool eco_requested_val = false;
    double engine_temp_val = 90.0;

    // Action effects
    void checkSensors_setDriveRpm() override {
        ++actions_logged;
        std::cout << "  [ECU Action] Chained: checkSensors() -> setDriveRpm() (Target: 2200 RPM)\n";
    }

    void checkSensors_setEcoRpm() override {
        ++actions_logged;
        std::cout << "  [ECU Action] Chained: checkSensors() -> setEcoRpm() (Target: 1400 RPM)\n";
    }

    void checkSensors_faultAlert() override {
        ++actions_logged;
        std::cout << "  [ECU Action] Chained: checkSensors() -> faultAlert() (Sensors Failed!)\n";
    }

    void activateCooling() override {
        ++actions_logged;
        std::cout << "  [ECU Action] activateCooling() -> Radiator fan 100% PWM\n";
    }

    void shutdownEngine() override {
        ++actions_logged;
        std::cout << "  [ECU Action] shutdownEngine() -> Injection cutoff\n";
    }

    void emergencyKill() override {
        ++actions_logged;
        std::cout << "  [ECU Action] emergencyKill() -> Main contactor open\n";
    }

    void restorePower() override {
        ++actions_logged;
        std::cout << "  [ECU Action] restorePower() -> Normal ignition timing\n";
    }
};

}  // namespace automotive::ecu

int main() {
    using namespace automotive::ecu;

    std::cout << "============================================================================\n"
              << " Showcase 06: Stateflow Digital Twin ECU & Zero-Heap Snapshot Recorder\n"
              << "============================================================================\n";

    EcuServices srv;
    EcuEngineFSM ecu;
    ecu.set_services(srv);

    // Snapshot recorder: Zero heap allocation, capacity = 16 snapshots
    ::fsm::snapshot_recorder<16, 512> recorder;

    // 1. Initial State: Standby
    std::cout << "\n[Step 1] Initializing Digital Twin ECU in Standby state...\n";
    VERIFY_EXPR(ecu.is_in_state<Standby>());
    VERIFY_EXPR(recorder.record(ecu, 100 /* Tag: Standby */, 1000));
    std::cout << "  -> Snapshot 1 recorded (Tag: 100 [Standby], Size: " << recorder.latest()->size << " bytes)\n";

    // 2. Chained Connective Junction Evaluation:
    // Standby -> [EvStart] -> Junction_Precheck [sensors_ok == 1] -> Junction_Mode [eco_requested == 0] -> Drive
    std::cout << "\n[Step 2] Dispatching EvStart (sensors_ok=true, eco_requested=false)...\n";
    srv.sensors_ok_val = true;
    srv.eco_requested_val = false;

    auto res = ecu.dispatch(EvStart{});
    VERIFY_EXPR(res.is_success());
    VERIFY_EXPR(ecu.is_in_state<Drive>());
    std::cout << "  -> Connective Junction Chaining Pass collapsed 2 intermediate junctions!\n";
    std::cout << "  -> Active state arrived atomically at: Drive\n";

    // Record checkpoint in Drive
    VERIFY_EXPR(recorder.record(ecu, 200 /* Tag: HealthyDrive */, 2000));
    std::cout << "  -> Snapshot 2 recorded (Tag: 200 [HealthyDrive])\n";

    // 3. Inject Overheating Anomaly
    std::cout << "\n[Step 3] Simulating coolant radiator failure: engine_temp rises to 112 deg C...\n";
    srv.engine_temp_val = 112.0;
    auto res_overheat = ecu.dispatch(EvOverheat{});
    VERIFY_EXPR(res_overheat.is_success());
    VERIFY_EXPR(ecu.is_in_state<LimpHome>());
    std::cout << "  -> Overheat guard triggered -> Active state transitioned to: LimpHome\n";

    // 4. Supervisory Digital Twin Time-Travel Rollback
    std::cout << "\n[Step 4] Supervisory HIL Digital Twin triggers Time-Travel Rollback...\n";
    std::cout << "  -> Restoring ECU to previous checkpoint 200 (HealthyDrive)...\n";
    VERIFY_EXPR(recorder.rewind_to_checkpoint(ecu, 200));

    VERIFY_EXPR(ecu.is_in_state<Drive>());
    std::cout << "  -> Rollback Successful! Active state restored to: Drive\n";

    // 5. Normal Shutdown
    std::cout << "\n[Step 5] Graceful engine shutdown...\n";
    auto res_stop = ecu.dispatch(EvStop{});
    VERIFY_EXPR(res_stop.is_success());
    VERIFY_EXPR(ecu.is_in_state<Standby>());
    std::cout << "  -> Active state returned to: Standby\n";

    std::cout << "\n============================================================================\n"
              << " [SUCCESS] Stateflow Digital Twin ECU & Snapshot Rollback Showcase PASSED!\n"
              << "============================================================================\n";

    return 0;
}

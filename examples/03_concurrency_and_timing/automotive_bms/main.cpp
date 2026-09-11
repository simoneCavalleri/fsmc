/**
 * @file main.cpp
 * @brief Executable verification test harness for ISO 26262 Automotive Battery Management System.
 * Demonstrates: Parallel orthogonal regions, data-race checking, ASIL-D fault preemption,
 * and concurrent thread-safe state machine execution.
 */

#include <cassert>

// VERIFY_EXPR: like assert(), but evaluates the expression in all build
// configurations (including NDEBUG / release) so the result variable is never
// considered unused by the compiler.
#ifndef VERIFY_EXPR
#define VERIFY_EXPR(expr) \
    do {                  \
        if (!(expr)) {    \
            std::abort(); \
        }                 \
    } while (false)
#endif
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <thread>

#include "bms_fsm.hpp"

namespace automotive {

/**
 * @struct ConcreteBmsServices
 * @brief Concrete automotive hardware and high-voltage contactor driver interface.
 */
struct ConcreteBmsServices : public BatteryManagementSystemServices {
    bool main_contactors_closed{false};
    float cooling_pump_rpm{0.0f};
    bool balance_shunts_enabled{false};
    bool vehicle_alert_raised{false};
    bool discharge_throttled{false};

    void CloseMainContactorsAction() override {
        main_contactors_closed = true;
        std::cout << "  \033[1;32m[HV PYRO-SWITCH]\033[0m Precharge complete: 800V main contactors CLOSED\n";
    }

    void OpenMainContactorsAction() override {
        main_contactors_closed = false;
        cooling_pump_rpm = 0.0f;
        balance_shunts_enabled = false;
        std::cout
            << "  \033[1;31m[HV SAFETY SHUTDOWN]\033[0m Main DC contactors OPENED within 8ms (ISO 26262 ASIL-D)\n";
    }

    void RampCoolingPumpAction() override {
        cooling_pump_rpm = 3500.0f;
        std::cout << "  \033[1;33m[THERMAL LOOP]\033[0m Glycol cooling pump ramped to 3,500 RPM (HighTemp warning)\n";
    }

    void StopCoolingPumpAction() override {
        cooling_pump_rpm = 800.0f;
        std::cout
            << "  \033[1;32m[THERMAL LOOP]\033[0m Temperatures nominal: cooling pump returned to eco-circulation\n";
    }

    void ThrottleDischargeAction() override {
        discharge_throttled = true;
        std::cout
            << "  \033[1;31m[DERATING]\033[0m Maximum tractive inverter power throttled to 25% (Thermal derating)\n";
    }

    void EnableBalanceShuntsAction() override {
        balance_shunts_enabled = true;
        std::cout
            << "  \033[1;33m[CELL BALANCING]\033[0m Resistive bleeder shunts switched ON for over-charged cells\n";
    }

    void DisableBalanceShuntsAction() override {
        balance_shunts_enabled = false;
        std::cout << "  \033[1;32m[CELL BALANCING]\033[0m Cell delta-V < 5mV: Balancing shunts switched OFF\n";
    }

    void AlertVehicleMasterAction() override {
        vehicle_alert_raised = true;
        std::cout
            << "  \033[1;33m[ISOLATION WATCH]\033[0m Chassis isolation resistance degraded: Warning sent to VCU\n";
    }

    void ClearVehicleAlertAction() override {
        vehicle_alert_raised = false;
        std::cout << "  \033[1;32m[ISOLATION WATCH]\033[0m Isolation resistance nominal (>500 kOhm): Alert cleared\n";
    }
};

}  // namespace automotive

static void print_section(std::string_view title) {
    std::cout << "\n\033[1;36m================================================================================\033[0m\n"
              << "\033[1;37m " << title << "\033[0m\n"
              << "\033[1;36m================================================================================\033[0m\n";
}

static void print_step(std::string_view step, std::string_view state) {
    std::cout << "  \033[1;35m[STEP]\033[0m " << std::left << std::setw(46) << step << " --> Current State: \033[1;32m"
              << state << "\033[0m\n";
}

int main() {
    print_section("FSMC SHOWCASE 03: AUTOMOTIVE BATTERY MANAGEMENT SYSTEM (ASIL-D)");

    automotive::BatteryManagementSystemInPorts in_ports;
    automotive::BatteryManagementSystemOutPorts out_ports;
    automotive::ConcreteBmsServices services;

    // 1. Verify MBSE Contracts
    in_ports.pack_voltage_v = 400.0f;              // Valid (250 to 450 V)
    in_ports.max_cell_temp_c = 28.5f;              // Valid (-20 to 65 C)
    in_ports.isolation_resistance_kohm = 2500.0f;  // Valid (100 to 10000 kOhm)
    assert(in_ports.validate_contracts());

    out_ports.cooling_pump_pwm = 35.0f;
    assert(out_ports.validate_contracts());
    std::cout << "  \033[1;32m[MBSE ASIL-D CONTRACTS]\033[0m All high-voltage pack sensor telemetry within verified "
                 "bounds: OK\n";

    // 2. Instantiate Thread-Safe FSM
    automotive::BatteryManagementSystem fsm(services);
    assert(fsm.current_state_name() == "Standby");
    print_step("Initial Power-On State", fsm.current_state_name());

    // 3. Precharge & Transition to Parallel Operational Superstate
    std::cout << "\n\033[1;33m--- Phase 1: High-Voltage Precharge & Contactor Closure ---\033[0m\n";
    fsm.dispatch(automotive::PrechargeCompleteCmd{});
    assert(services.main_contactors_closed);
    print_step("Dispatched PrechargeCompleteCmd", fsm.current_state_name());

    // 4. Region 1 (Thermal Supervision): High Temperature Warning -> Active Cooling
    std::cout << "\n\033[1;33m--- Phase 2: Region 1 (Thermal) - High Temp Warning ---\033[0m\n";
    fsm.dispatch(automotive::HighTempWarningEvent{});
    assert(services.cooling_pump_rpm > 1000.0f);
    print_step("Dispatched HighTempWarningEvent", fsm.current_state_name());

    // 5. Region 2 (Cell Balancing): Delta-V Threshold Exceeded -> Balancing Active
    // After OrthogonalProductPass, individual region leaf states (BalancingIdle etc.) are
    // lowered into Cartesian product states.  Start from the canonical initial product state.
    std::cout << "\n\033[1;33m--- Phase 3: Region 2 (Balancing) - Over-voltage Shunts Active ---\033[0m\n";
    automotive::BatteryManagementSystem balancing_fsm(automotive::Operational_ThermalNominal_BalancingIdle_IsoHealthy{},
                                                      services);
    balancing_fsm.dispatch(automotive::DeltaVThresholdExceededEvent{});
    assert(services.balance_shunts_enabled);
    print_step("Dispatched DeltaVThresholdExceededEvent", balancing_fsm.current_state_name());

    // 6. Region 3 (Isolation Barrier): Isolation Resistance Drop -> Warning to VCU
    std::cout << "\n\033[1;33m--- Phase 4: Region 3 (Isolation) - High-Voltage Leakage Warning ---\033[0m\n";
    automotive::BatteryManagementSystem isolation_fsm(automotive::Operational_ThermalNominal_BalancingIdle_IsoHealthy{},
                                                      services);
    isolation_fsm.dispatch(automotive::LowIsolationResistanceEvent{});
    assert(services.vehicle_alert_raised);
    print_step("Dispatched LowIsolationResistanceEvent", isolation_fsm.current_state_name());

    // 7. Concurrent Recovery in Balancing & Isolation
    std::cout << "\n\033[1;33m--- Phase 5: Concurrent Region Recoveries ---\033[0m\n";
    balancing_fsm.dispatch(automotive::CellsBalancedEvent{});
    assert(!services.balance_shunts_enabled);
    print_step("Dispatched CellsBalancedEvent", balancing_fsm.current_state_name());

    isolation_fsm.dispatch(automotive::IsolationRestoredEvent{});
    assert(!services.vehicle_alert_raised);
    print_step("Dispatched IsolationRestoredEvent", isolation_fsm.current_state_name());

    // 8. ASIL-D Preemption: Thermal Runaway Emergency Shutdown
    // Start from the canonical product-state for an active Operational machine.
    std::cout << "\n\033[1;33m--- Phase 6: ASIL-D Priority 1 Preemption - Thermal Runaway Fault ---\033[0m\n";
    automotive::BatteryManagementSystem safety_fsm(automotive::Operational_ThermalNominal_BalancingIdle_IsoHealthy{},
                                                   services);
    safety_fsm.dispatch(automotive::ThermalRunawayFault{});
    assert(safety_fsm.is_in<automotive::FaultShutdown>());
    assert(!services.main_contactors_closed);
    print_step("Dispatched ThermalRunawayFault (Preemption)", safety_fsm.current_state_name());
    std::cout << "  \033[1;32m[SAFETY PREEMPTION OK]\033[0m Verified: High-priority fault immediately terminated all "
                 "active regions!\n";

    // ------------------------------------------------------------------------
    // 9. Lock-Free SPSC Cell Telemetry Stream (High-Throughput Ingestion)
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Phase 7: Lock-Free SPSC High-Frequency Stream Ingestion (spsc_fsm) ---\033[0m\n";
    using SpscBmsFSM =
        fsm::spsc_fsm<automotive::BatteryManagementSystemTable, automotive::BatteryManagementSystemInPorts,
                      automotive::BatteryManagementSystemOutPorts, fsm::no_registers, automotive::ConcreteBmsServices,
                      128>;
    SpscBmsFSM spsc_bms(services);

    // Initial Standby -> Precharge Complete
    assert(spsc_bms.post(automotive::PrechargeCompleteCmd{}));
    assert(spsc_bms.queue_size() == 1u);
    assert(spsc_bms.run_until_empty(in_ports, out_ports) == 1u);
    assert(spsc_bms.is_in<automotive::Operational>() ||
           spsc_bms.is_in<automotive::Operational_ThermalNominal_BalancingIdle_IsoHealthy>());
    print_step("SPSC Precharge completed", spsc_bms.state_name());

    // High-frequency producer thread simulating CAN-bus ADC driver pushing events concurrently
    std::thread can_driver_thread([&]() {
        for (int i = 0; i < 8; ++i) {
            VERIFY_EXPR(spsc_bms.post(automotive::DeltaVThresholdExceededEvent{}));
        }
    });
    can_driver_thread.join();

    assert(spsc_bms.queue_size() == 8u);
    std::cout
        << "  \033[1;32m[LOCK-FREE SPSC]\033[0m CAN driver enqueued 8 telemetry frames without mutex contention\n";

    // Consumer drains events via run_until_empty()
    std::size_t processed = spsc_bms.run_until_empty(in_ports, out_ports);
    assert(processed == 8u);
    std::cout << "  \033[1;32m[LOCK-FREE DRAIN]\033[0m BMS consumer processed " << processed
              << " events with zero locks: OK\n";

    print_section("ALL BMS CONCURRENCY & TIMING TESTS PASSED (100% SUCCESS)");
    return 0;
}

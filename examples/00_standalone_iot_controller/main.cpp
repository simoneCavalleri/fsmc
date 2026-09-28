/**
 * @file main.cpp
 * @brief Showcase 00: Pure Modern C++20 Standalone Finite State Machine.
 *
 * Demonstrates:
 * - 100% Pure C++20 Header-Only runtime (zero diagram compilation required).
 * - Strongly-typed event payloads (passing rich data directly into events).
 * - Transition guards and actions consuming event payloads and internal datapath registers.
 * - State lifecycle on_enter / on_exit hooks with zero heap allocations.
 * - Exhaustive inspection of dispatch_result (is_success, is_guard_rejected, is_unhandled).
 * - Safe error handling, emergency preemption, and fail-safe recovery.
 */

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "fsm/backend/cpp/runtime/fsm.hpp"

#define VERIFY_EXPR(cond)                                                                             \
    do {                                                                                              \
        if (!(cond)) {                                                                                \
            std::cerr << "Verification failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::abort();                                                                             \
        }                                                                                             \
    } while (0)

namespace iot {

// ============================================================================
// 1. Domain States
// ============================================================================
struct StateIdle {
    static constexpr std::string_view name = "Idle";
};

struct StateHeating {
    static constexpr std::string_view name = "Heating";

    void on_enter() const {
        std::cout << "  \033[1;32m[HVAC RELAY]\033[0m Furnace burner ignited & circulation blower energized\n";
    }

    void on_exit() const {
        std::cout << "  \033[1;33m[HVAC RELAY]\033[0m Furnace fuel valve shut off; running post-purge fan (30s)\n";
    }
};

struct StateCooling {
    static constexpr std::string_view name = "Cooling";

    void on_enter() const {
        std::cout << "  \033[1;36m[HVAC RELAY]\033[0m AC scroll compressor started & evaporator fan running\n";
    }

    void on_exit() const {
        std::cout << "  \033[1;33m[HVAC RELAY]\033[0m AC compressor stopped; equalization delay engaged\n";
    }
};

struct StateThermalEmergency {
    static constexpr std::string_view name = "ThermalEmergency";

    void on_enter() const {
        std::cout
            << "  \033[1;31m[SAFETY CUTOFF]\033[0m Thermal emergency tripped! All heating/cooling relays forced OFF!\n";
    }
};

// ============================================================================
// 2. Strongly-Typed Event Payloads
// ============================================================================

/**
 * @brief High-frequency sensor packet containing physical telemetry.
 */
struct TemperatureTelemetry {
    float current_temp_c{21.0f};
    float relative_humidity{45.0f};
    uint32_t sensor_id{101};
};

/**
 * @brief User or cloud command setting desired comfort target.
 */
struct TargetSetCmd {
    float target_temp_c{21.0f};
    bool eco_mode{false};
};

/**
 * @brief Manual or automated safety shutdown command.
 */
struct EmergencyStopCmd {
    std::string_view reason{"Manual shutdown"};
};

/**
 * @brief Operator acknowledge & safety clear command.
 */
struct ResumeCmd {
    uint32_t operator_pin{1234};
};

// ============================================================================
// 3. Hardware I/O Ports & Datapath Registers
// ============================================================================

struct ThermostatInPorts {
    bool power_grid_nominal{true};
    bool intake_filter_clean{true};
};

struct ThermostatOutPorts {
    bool furnace_relay{false};
    bool ac_compressor_relay{false};
    bool alarm_buzzer{false};
};

struct ThermostatRegisters {
    float target_temp_c{21.0f};
    float current_temp_c{21.0f};
    float hysteresis_c{1.0f};
    int heating_cycles{0};
    int cooling_cycles{0};
    int emergency_trips{0};
    bool eco_mode{false};
};

// ============================================================================
// 4. Custom Guard Functors
// ============================================================================

struct NeedsHeatingGuard {
    [[nodiscard]] bool operator()(const TemperatureTelemetry& evt, const ThermostatInPorts& in,
                                  const ThermostatRegisters& reg) const noexcept {
        return in.power_grid_nominal && (evt.current_temp_c < (reg.target_temp_c - reg.hysteresis_c));
    }
};

struct NeedsCoolingGuard {
    [[nodiscard]] bool operator()(const TemperatureTelemetry& evt, const ThermostatInPorts& in,
                                  const ThermostatRegisters& reg) const noexcept {
        return in.power_grid_nominal && (evt.current_temp_c > (reg.target_temp_c + reg.hysteresis_c));
    }
};

struct TemperatureInBandGuard {
    [[nodiscard]] bool operator()(const TemperatureTelemetry& evt, const ThermostatRegisters& reg) const noexcept {
        return std::abs(evt.current_temp_c - reg.target_temp_c) <= 0.5f;
    }
};

struct OverheatCriticalGuard {
    [[nodiscard]] constexpr bool operator()(const TemperatureTelemetry& evt) const noexcept {
        return evt.current_temp_c >= 45.0f;
    }
};

struct ValidOperatorPinGuard {
    [[nodiscard]] constexpr bool operator()(const ResumeCmd& cmd, const ThermostatInPorts& in) const noexcept {
        return in.power_grid_nominal && (cmd.operator_pin == 1234);
    }
};

// ============================================================================
// 5. Custom Action Functors
// ============================================================================

struct StartFurnaceAction {
    void operator()(const TemperatureTelemetry& evt, ThermostatOutPorts& out, ThermostatRegisters& reg) const {
        out.furnace_relay = true;
        out.ac_compressor_relay = false;
        reg.current_temp_c = evt.current_temp_c;
        reg.heating_cycles++;
        std::cout << "  \033[1;32m[ACTION]\033[0m Target " << reg.target_temp_c << "°C vs Ambient "
                  << evt.current_temp_c << "°C -> Furnace ON (Cycle #" << reg.heating_cycles << ")\n";
    }
};

struct StopFurnaceAction {
    void operator()(const TemperatureTelemetry& evt, ThermostatOutPorts& out, ThermostatRegisters& reg) const {
        out.furnace_relay = false;
        reg.current_temp_c = evt.current_temp_c;
        std::cout << "  \033[1;32m[ACTION]\033[0m Equilibrium reached at " << evt.current_temp_c
                  << "°C -> Furnace OFF\n";
    }
};

struct StartAcAction {
    void operator()(const TemperatureTelemetry& evt, ThermostatOutPorts& out, ThermostatRegisters& reg) const {
        out.ac_compressor_relay = true;
        out.furnace_relay = false;
        reg.current_temp_c = evt.current_temp_c;
        reg.cooling_cycles++;
        std::cout << "  \033[1;36m[ACTION]\033[0m Target " << reg.target_temp_c << "°C vs Ambient "
                  << evt.current_temp_c << "°C -> AC Compressor ON (Cycle #" << reg.cooling_cycles << ")\n";
    }
};

struct StopAcAction {
    void operator()(const TemperatureTelemetry& evt, ThermostatOutPorts& out, ThermostatRegisters& reg) const {
        out.ac_compressor_relay = false;
        reg.current_temp_c = evt.current_temp_c;
        std::cout << "  \033[1;36m[ACTION]\033[0m Comfort band reached at " << evt.current_temp_c
                  << "°C -> AC Compressor OFF\n";
    }
};

struct UpdateTargetAction {
    void operator()(const TargetSetCmd& cmd, ThermostatRegisters& reg) const {
        reg.target_temp_c = cmd.target_temp_c;
        reg.eco_mode = cmd.eco_mode;
        std::cout << "  \033[1;35m[CONFIG]\033[0m New comfort setpoint configured: " << cmd.target_temp_c
                  << "°C (Eco: " << (cmd.eco_mode ? "ON" : "OFF") << ")\n";
    }
};

struct EmergencyCutoffAction {
    void operator()(ThermostatOutPorts& out, ThermostatRegisters& reg) const {
        out.furnace_relay = false;
        out.ac_compressor_relay = false;
        out.alarm_buzzer = true;
        reg.emergency_trips++;
        std::cout << "  \033[1;31m[ALARM]\033[0m Safety interlock trip #" << reg.emergency_trips
                  << " -> Sounding buzzer & isolating high-voltage lines\n";
    }
};

struct ResetAlarmAction {
    void operator()(ThermostatOutPorts& out) const {
        out.alarm_buzzer = false;
        std::cout << "  \033[1;32m[RECOVERY]\033[0m Operator pin verified; alarm buzzer muted & system restored\n";
    }
};

// ============================================================================
// 6. Modern C++20 Static Transition Table Definition
// ============================================================================

using ThermostatTable = fsm::transition_table<
    // ------------------------------------------------------------------------
    // Transitions from StateIdle
    // ------------------------------------------------------------------------
    fsm::row<StateIdle, TemperatureTelemetry, StateHeating>::when<NeedsHeatingGuard>::then<StartFurnaceAction>,
    fsm::row<StateIdle, TemperatureTelemetry, StateCooling>::when<NeedsCoolingGuard>::then<StartAcAction>,
    fsm::row<StateIdle, TemperatureTelemetry,
             StateThermalEmergency>::when<OverheatCriticalGuard>::then<EmergencyCutoffAction>,
    fsm::internal_row<StateIdle, TargetSetCmd>::then<UpdateTargetAction>,
    fsm::row<StateIdle, EmergencyStopCmd, StateThermalEmergency>::then<EmergencyCutoffAction>,

    // ------------------------------------------------------------------------
    // Transitions from StateHeating
    // ------------------------------------------------------------------------
    fsm::row<StateHeating, TemperatureTelemetry, StateIdle>::when<TemperatureInBandGuard>::then<StopFurnaceAction>,
    fsm::row<StateHeating, TemperatureTelemetry,
             StateThermalEmergency>::when<OverheatCriticalGuard>::then<EmergencyCutoffAction>,
    fsm::row<StateHeating, EmergencyStopCmd, StateThermalEmergency>::then<EmergencyCutoffAction>,

    // ------------------------------------------------------------------------
    // Transitions from StateCooling
    // ------------------------------------------------------------------------
    fsm::row<StateCooling, TemperatureTelemetry, StateIdle>::when<TemperatureInBandGuard>::then<StopAcAction>,
    fsm::row<StateCooling, TemperatureTelemetry,
             StateThermalEmergency>::when<OverheatCriticalGuard>::then<EmergencyCutoffAction>,
    fsm::row<StateCooling, EmergencyStopCmd, StateThermalEmergency>::then<EmergencyCutoffAction>,

    // ------------------------------------------------------------------------
    // Transitions from StateThermalEmergency (Recovery Path)
    // ------------------------------------------------------------------------
    fsm::row<StateThermalEmergency, ResumeCmd, StateIdle>::when<ValidOperatorPinGuard>::then<ResetAlarmAction>>;

// Typedef the concrete FSM using policy configuration
using ThermostatFSM = fsm::fsm<ThermostatTable, ThermostatInPorts, ThermostatOutPorts, ThermostatRegisters>;

}  // namespace iot

// ============================================================================
// Visual Pretty-Printing Helpers
// ============================================================================
static void print_banner(std::string_view title) {
    std::cout << "\n\033[1;36m================================================================================\033[0m\n"
              << "\033[1;37m " << title << "\033[0m\n"
              << "\033[1;36m================================================================================\033[0m\n";
}

static void print_step(std::string_view scenario, std::string_view state) {
    std::cout << "  \033[1;35m[STATUS]\033[0m " << std::left << std::setw(42) << scenario
              << " --> Active State: \033[1;32m" << state << "\033[0m\n";
}

// ============================================================================
// Main Simulation Demonstration
// ============================================================================
int main() {
    print_banner("FSMC SHOWCASE 00: PURE MODERN C++20 STANDALONE IOT THERMOSTAT");

    iot::ThermostatInPorts in_ports;
    iot::ThermostatOutPorts out_ports;
    iot::ThermostatRegisters registers;
    registers.target_temp_c = 21.0f;
    registers.current_temp_c = 21.0f;
    registers.hysteresis_c = 1.0f;

    iot::ThermostatFSM sm(registers);

    // Initial State Check
    VERIFY_EXPR(sm.current_state_name() == "Idle");
    VERIFY_EXPR(sm.is_in_state<iot::StateIdle>());
    print_step("Initial Power-On & Telemetry Check", sm.current_state_name());

    // ------------------------------------------------------------------------
    // Scenario 1: Nominal Telemetry in Equilibrium (Guard Rejection)
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Scenario 1: Equilibrium Telemetry (Guard Rejection / No Transition) ---\033[0m\n";
    auto res1 = sm.dispatch(iot::TemperatureTelemetry{21.2f, 44.0f, 101}, in_ports, out_ports);
    VERIFY_EXPR(res1.is_guard_rejected());
    VERIFY_EXPR(sm.is_in_state<iot::StateIdle>());
    std::cout << "  \033[1;30m[DISPATCH RESULT]\033[0m Status: " << res1.to_string()
              << " (Temp 21.2°C is within hysteresis band [20°C - 22°C])\n";
    print_step("Equilibrium telemetry processed", sm.current_state_name());

    // ------------------------------------------------------------------------
    // Scenario 2: Ambient Temp Drops -> NeedsHeatingGuard Triggers Heating State
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Scenario 2: Temperature Drop -> Heating State Activated ---\033[0m\n";
    auto res2 = sm.dispatch(iot::TemperatureTelemetry{19.4f, 40.0f, 101}, in_ports, out_ports);
    VERIFY_EXPR(res2.is_success());
    VERIFY_EXPR(sm.is_in_state<iot::StateHeating>());
    VERIFY_EXPR(out_ports.furnace_relay == true);
    VERIFY_EXPR(sm.registers().heating_cycles == 1);
    std::cout << "  \033[1;30m[DISPATCH RESULT]\033[0m Status: " << res2.to_string()
              << " | Trace: " << res2.trace->source << " -> " << res2.trace->target << "\n";
    print_step("Furnace energized", sm.current_state_name());

    // ------------------------------------------------------------------------
    // Scenario 3: Temperature Rises to Target -> Returns to Idle
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Scenario 3: Target Reached -> StateHeating Exit to Idle ---\033[0m\n";
    auto res3 = sm.dispatch(iot::TemperatureTelemetry{21.1f, 42.0f, 101}, in_ports, out_ports);
    VERIFY_EXPR(res3.is_success());
    VERIFY_EXPR(sm.is_in_state<iot::StateIdle>());
    VERIFY_EXPR(out_ports.furnace_relay == false);
    (void)res3;
    print_step("Equilibrium restored", sm.current_state_name());

    // ------------------------------------------------------------------------
    // Scenario 4: User Reconfigures Target (Self-Transition with Payload)
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Scenario 4: User Setpoint Update (Self-Transition) ---\033[0m\n";
    auto res4 = sm.dispatch(iot::TargetSetCmd{19.0f, true}, in_ports, out_ports);
    VERIFY_EXPR(res4.is_success());
    VERIFY_EXPR(sm.is_in_state<iot::StateIdle>());
    VERIFY_EXPR(sm.registers().target_temp_c == 19.0f);
    VERIFY_EXPR(sm.registers().eco_mode == true);
    (void)res4;
    print_step("New setpoint configured (19.0°C)", sm.current_state_name());

    // ------------------------------------------------------------------------
    // Scenario 5: Heatwave Spikes Temp to 23.5°C -> Triggers Cooling State
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Scenario 5: Ambient Temp Exceeds Setpoint + Hysteresis -> Cooling ---\033[0m\n";
    auto res5 = sm.dispatch(iot::TemperatureTelemetry{23.5f, 58.0f, 101}, in_ports, out_ports);
    VERIFY_EXPR(res5.is_success());
    VERIFY_EXPR(sm.is_in_state<iot::StateCooling>());
    VERIFY_EXPR(out_ports.ac_compressor_relay == true);
    VERIFY_EXPR(sm.registers().cooling_cycles == 1);
    (void)res5;
    print_step("AC compressor active", sm.current_state_name());

    // ------------------------------------------------------------------------
    // Scenario 6: Critical Overheat Alarm (Emergency Preemption)
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Scenario 6: Overheat Sensor Anomaly (48.0°C) -> Emergency Preemption ---\033[0m\n";
    auto res6 = sm.dispatch(iot::TemperatureTelemetry{48.0f, 60.0f, 101}, in_ports, out_ports);
    VERIFY_EXPR(res6.is_success());
    VERIFY_EXPR(sm.is_in_state<iot::StateThermalEmergency>());
    VERIFY_EXPR(out_ports.ac_compressor_relay == false);
    VERIFY_EXPR(out_ports.furnace_relay == false);
    VERIFY_EXPR(out_ports.alarm_buzzer == true);
    (void)res6;
    print_step("Emergency shutdown engaged", sm.current_state_name());

    // ------------------------------------------------------------------------
    // Scenario 7: Illegal Event Rejection in Emergency State (Fail-Safe Verification)
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Scenario 7: Normal Commands Rejected While in Emergency State ---\033[0m\n";
    auto res7 = sm.dispatch(iot::TargetSetCmd{22.0f, false}, in_ports, out_ports);
    VERIFY_EXPR(res7.is_unhandled());
    VERIFY_EXPR(sm.is_in_state<iot::StateThermalEmergency>());
    std::cout << "  \033[1;30m[DISPATCH RESULT]\033[0m Status: " << res7.to_string()
              << " (Correctly rejected TargetSetCmd while safety interlock is tripped)\n";
    (void)res7;
    print_step("Fail-safe rejection confirmed", sm.current_state_name());

    // ------------------------------------------------------------------------
    // Scenario 8: Operator PIN Verification & Recovery
    // ------------------------------------------------------------------------
    std::cout << "\n\033[1;33m--- Scenario 8: Authorized Operator Resume & Alarm Reset ---\033[0m\n";
    auto res8 = sm.dispatch(iot::ResumeCmd{1234}, in_ports, out_ports);
    VERIFY_EXPR(res8.is_success());
    VERIFY_EXPR(sm.is_in_state<iot::StateIdle>());
    VERIFY_EXPR(out_ports.alarm_buzzer == false);
    (void)res8;
    print_step("Safety interlock cleared -> Returned to Idle", sm.current_state_name());

    print_banner("ALL STANDALONE IOT THERMOSTAT SIMULATION CHECKS PASSED [100% OK]");
    return 0;
}

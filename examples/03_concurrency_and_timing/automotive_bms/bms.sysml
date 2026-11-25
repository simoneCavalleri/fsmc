// ============================================================================
// OMG SysML v2 State Machine: High-Voltage Automotive BMS (ISO 26262 ASIL-D)
// Domain: Automotive Electric Powertrain & Battery Safety Management
// Demonstrating: Parallel Orthogonal Regions, Clocks, Time Invariants, Race Checking
// ============================================================================

package AutomotiveBms {
    private import ScalarValues::*;

    state def BatteryManagementSystem {
        // Typed MBSE I/O Parameters with Physical Units and ASIL-D Bounds
        in pack_voltage_v : Real { assert constraint { self >= 250.0 and self <= 450.0; } }
        in max_cell_temp_c : Real { assert constraint { self >= -20.0 and self <= 65.0; } }
        in isolation_resistance_kohm : Real { assert constraint { self >= 100.0 and self <= 10000.0; } }
        out cooling_pump_pwm : Real { assert constraint { self >= 0.0 and self <= 100.0; } }
        out contactors_closed : Boolean;
        out balancing_active : Boolean;

        entry; then Standby;

        state Standby;

        transition first Standby accept PrechargeCompleteCmd
            do CloseMainContactorsAction then Operational;

        // Concurrent Parallel Orthogonal State with 3 Independent Supervision Regions
        state Operational parallel {
            // Region 1: Thermal Supervision & Dynamic Cooling Loop
            state ThermalSupervision {
                entry; then ThermalNominal;

                state ThermalNominal {
                    // @fsm:state time_invariant="100[ms]"
                }

                state ActiveCooling {
                    // @fsm:state time_invariant="50[ms]"
                }

                state ThermalDerating;

                // @fsm:priority 5
                transition t_cool_start
                    first ThermalNominal
                    accept HighTempWarningEvent
                    do RampCoolingPumpAction
                    then ActiveCooling;

                // @fsm:priority 5
                transition t_cool_derate
                    first ActiveCooling
                    accept CriticalTempEvent
                    do ThrottleDischargeAction
                    then ThermalDerating;

                // @fsm:priority 10
                transition t_cool_norm
                    first ActiveCooling
                    accept TempNominalEvent
                    do StopCoolingPumpAction
                    then ThermalNominal;
            }

            // Region 2: Multi-Cell Voltage Balancing
            state CellBalancing {
                entry; then BalancingIdle;

                state BalancingIdle;

                state BalancingActive {
                    // @fsm:state time_invariant="500[ms]"
                }

                // @fsm:priority 5
                transition t_bal_start
                    first BalancingIdle
                    accept DeltaVThresholdExceededEvent
                    do EnableBalanceShuntsAction
                    then BalancingActive;

                // @fsm:priority 5
                transition t_bal_stop
                    first BalancingActive
                    accept CellsBalancedEvent
                    do DisableBalanceShuntsAction
                    then BalancingIdle;
            }

            // Region 3: High-Voltage Chassis Isolation Barrier Monitoring
            state IsolationMonitoring {
                entry; then IsoHealthy;

                state IsoHealthy {
                    // @fsm:state time_invariant="200[ms]"
                }

                state IsoWarning;

                // @fsm:priority 5
                transition t_iso_degrade
                    first IsoHealthy
                    accept LowIsolationResistanceEvent
                    do AlertVehicleMasterAction
                    then IsoWarning;

                // @fsm:priority 5
                transition t_iso_restore
                    first IsoWarning
                    accept IsolationRestoredEvent
                    do ClearVehicleAlertAction
                    then IsoHealthy;
            }
        }

        // Global ASIL-D Emergency Shutdown State
        state FaultShutdown {
            entry action OpenMainContactorsAction;
        }

        // High-Priority Safety Interlock Preemption from Any Operational Region
        // @fsm:priority 1
        transition t_thermal_runaway
            first Operational
            accept ThermalRunawayFault
            do OpenMainContactorsAction
            then FaultShutdown;

        // @fsm:priority 1
        transition t_hv_estop
            first Operational
            accept HvInterlockBreachedFault
            do OpenMainContactorsAction
            then FaultShutdown;
    }
}

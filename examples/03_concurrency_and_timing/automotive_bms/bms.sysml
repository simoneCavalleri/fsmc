// ============================================================================
// OMG SysML v2 State Machine: High-Voltage Automotive BMS (ISO 26262 ASIL-D)
// Domain: Automotive Electric Powertrain & Battery Safety Management
// Demonstrating: Parallel Orthogonal Regions, Clocks, Time Invariants, Race Checking
// ============================================================================

package AutomotiveBms {
    state def BatteryManagementSystem {
        // Typed MBSE I/O Ports with Physical Units and ASIL-D Bounds
        in port pack_voltage_v : Real { assert constraint { self >= 250.0 and self <= 450.0; } }
        in port max_cell_temp_c : Real { assert constraint { self >= -20.0 and self <= 65.0; } }
        in port isolation_resistance_kohm : Real { assert constraint { self >= 100.0 and self <= 10000.0; } }
        out port cooling_pump_pwm : Real { assert constraint { self >= 0.0 and self <= 100.0; } }
        out port contactors_closed : Boolean;
        out port balancing_active : Boolean;

        entry; then Standby;

        state Standby;

        transition from Standby accept PrechargeCompleteCmd 
            do CloseMainContactorsAction then Operational;

        // Concurrent Parallel Orthogonal State with 3 Independent Supervision Regions
        parallel state Operational {
            // Region 1: Thermal Supervision & Dynamic Cooling Loop
            state ThermalSupervision {
                entry; then ThermalNominal;

                state ThermalNominal {
                    stay duration <= 100[ms];
                }

                state ActiveCooling {
                    stay duration <= 50[ms];
                }

                state ThermalDerating;

                transition t_cool_start
                    priority 5
                    first ThermalNominal
                    accept HighTempWarningEvent
                    do RampCoolingPumpAction
                    then ActiveCooling;

                transition t_cool_derate
                    priority 5
                    first ActiveCooling
                    accept CriticalTempEvent
                    do ThrottleDischargeAction
                    then ThermalDerating;

                transition t_cool_norm
                    priority 10
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
                    stay duration <= 500[ms];
                }

                transition t_bal_start
                    priority 5
                    first BalancingIdle
                    accept DeltaVThresholdExceededEvent
                    do EnableBalanceShuntsAction
                    then BalancingActive;

                transition t_bal_stop
                    priority 5
                    first BalancingActive
                    accept CellsBalancedEvent
                    do DisableBalanceShuntsAction
                    then BalancingIdle;
            }

            // Region 3: High-Voltage Chassis Isolation Barrier Monitoring
            state IsolationMonitoring {
                entry; then IsoHealthy;

                state IsoHealthy {
                    stay duration <= 200[ms];
                }

                state IsoWarning;

                transition t_iso_degrade
                    priority 5
                    first IsoHealthy
                    accept LowIsolationResistanceEvent
                    do AlertVehicleMasterAction
                    then IsoWarning;

                transition t_iso_restore
                    priority 5
                    first IsoWarning
                    accept IsolationRestoredEvent
                    do ClearVehicleAlertAction
                    then IsoHealthy;
            }
        }

        // Global ASIL-D Emergency Shutdown State
        state FaultShutdown {
            entry / OpenMainContactorsAction;
        }

        // High-Priority Safety Interlock Preemption from Any Operational Region
        transition t_thermal_runaway
            priority 1
            first Operational
            accept ThermalRunawayFault
            do OpenMainContactorsAction
            then FaultShutdown;

        transition t_hv_estop
            priority 1
            first Operational
            accept HvInterlockBreachedFault
            do OpenMainContactorsAction
            then FaultShutdown;
    }
}

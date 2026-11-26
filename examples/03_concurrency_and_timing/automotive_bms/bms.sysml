// ============================================================================
// OMG SysML v2.0 State Machine: High-Voltage Automotive BMS (ISO 26262 ASIL-D)
// Domain: Automotive Electric Powertrain & Battery Safety Management
// Demonstrating: Parallel Orthogonal Regions, Time Invariants, Race Checking
// ============================================================================

package AutomotiveBms {
    private import ScalarValues::*;
    private import SI::*;
    private import Time::DurationOf;

    metadata def Priority {
        attribute value : Natural;
    }

    item def PrechargeCompleteCmd;
    item def HighTempWarningEvent;
    item def CriticalTempEvent;
    item def TempNominalEvent;
    item def DeltaVThresholdExceededEvent;
    item def CellsBalancedEvent;
    item def LowIsolationResistanceEvent;
    item def IsolationRestoredEvent;
    item def ThermalRunawayFault;
    item def HvInterlockBreachedFault;

    state def BatteryManagementSystem {
        // Typed MBSE I/O Parameters with Physical Units and ASIL-D Bounds
        in pack_voltage_v : Real {
            assert constraint { pack_voltage_v >= 250.0 and pack_voltage_v <= 450.0 }
        }
        in max_cell_temp_c : Real {
            assert constraint { max_cell_temp_c >= -20.0 and max_cell_temp_c <= 65.0 }
        }
        in isolation_resistance_kohm : Real {
            assert constraint { isolation_resistance_kohm >= 100.0 and isolation_resistance_kohm <= 10000.0 }
        }
        out cooling_pump_pwm : Real {
            assert constraint { cooling_pump_pwm >= 0.0 and cooling_pump_pwm <= 100.0 }
        }
        out contactors_closed : Boolean;
        out balancing_active : Boolean;

        entry; then Standby;

        state Standby;

        transition first Standby accept PrechargeCompleteCmd
            do action CloseMainContactorsAction then Operational;

        // Concurrent Parallel Orthogonal State with 3 Independent Supervision Regions
        state Operational parallel {
            // Region 1: Thermal Supervision & Dynamic Cooling Loop
            state ThermalSupervision {
                entry; then ThermalNominal;

                state ThermalNominal {
                    assert constraint { DurationOf(ThermalNominal) <= 100 [ms] }
                }

                state ActiveCooling {
                    assert constraint { DurationOf(ActiveCooling) <= 50 [ms] }
                }

                state ThermalDerating;

                transition t_cool_start
                    first ThermalNominal
                    accept HighTempWarningEvent
                    do action RampCoolingPumpAction
                    then ActiveCooling {
                        @Priority { value = 5; }
                    }

                transition t_cool_derate
                    first ActiveCooling
                    accept CriticalTempEvent
                    do action ThrottleDischargeAction
                    then ThermalDerating {
                        @Priority { value = 5; }
                    }

                transition t_cool_norm
                    first ActiveCooling
                    accept TempNominalEvent
                    do action StopCoolingPumpAction
                    then ThermalNominal {
                        @Priority { value = 10; }
                    }
            }

            // Region 2: Multi-Cell Voltage Balancing
            state CellBalancing {
                entry; then BalancingIdle;

                state BalancingIdle;

                state BalancingActive {
                    assert constraint { DurationOf(BalancingActive) <= 500 [ms] }
                }

                transition t_bal_start
                    first BalancingIdle
                    accept DeltaVThresholdExceededEvent
                    do action EnableBalanceShuntsAction
                    then BalancingActive {
                        @Priority { value = 5; }
                    }

                transition t_bal_stop
                    first BalancingActive
                    accept CellsBalancedEvent
                    do action DisableBalanceShuntsAction
                    then BalancingIdle {
                        @Priority { value = 5; }
                    }
            }

            // Region 3: High-Voltage Chassis Isolation Barrier Monitoring
            state IsolationMonitoring {
                entry; then IsoHealthy;

                state IsoHealthy {
                    assert constraint { DurationOf(IsoHealthy) <= 200 [ms] }
                }

                state IsoWarning;

                transition t_iso_degrade
                    first IsoHealthy
                    accept LowIsolationResistanceEvent
                    do action AlertVehicleMasterAction
                    then IsoWarning {
                        @Priority { value = 5; }
                    }

                transition t_iso_restore
                    first IsoWarning
                    accept IsolationRestoredEvent
                    do action ClearVehicleAlertAction
                    then IsoHealthy {
                        @Priority { value = 5; }
                    }
            }
        }

        // Global ASIL-D Emergency Shutdown State
        state FaultShutdown {
            entry action OpenMainContactorsAction;
        }

        // High-Priority Safety Interlock Preemption from Any Operational Region
        transition t_thermal_runaway
            first Operational
            accept ThermalRunawayFault
            do action OpenMainContactorsAction
            then FaultShutdown {
                @Priority { value = 1; }
            }

        transition t_hv_estop
            first Operational
            accept HvInterlockBreachedFault
            do action OpenMainContactorsAction
            then FaultShutdown {
                @Priority { value = 1; }
            }
    }
}

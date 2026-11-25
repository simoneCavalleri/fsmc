// ============================================================================
// OMG SysML v2.0 Multi-FSM Thermal Subsystem
// Demonstrates:
//   - Multi-file `private import SharedProtocol::*;`
//   - Conjugated `port def` (`~ThermalLinkPort` vs `ThermalLinkPort`)
//   - Two `state def`s in one specification (`--list-machines`, `--state-def`)
//   - Executable `item msg : EvOverTemp { ... } send msg via link;` zero-heap outbox
// ============================================================================

package ThermalSubsystem {
    private import ScalarValues::*;
    private import SharedProtocol::*;

    part def SensorUnit {
        port link : ~ThermalLinkPort;
        exhibit state sm : SensorMonitorFsm;
    }

    part def CoolerUnit {
        port link : ThermalLinkPort;
        exhibit state sm : CoolingActuatorFsm;
    }

    state def SensorMonitorFsm {
        in sensor_temp_c : Integer;
        port link : ~ThermalLinkPort;
        attribute alerts_sent : Integer := 0;

        entry; then Monitoring;

        state Monitoring;
        state OverTempLatched;

        transition t_overtemp
            first Monitoring
            accept EvSample
            if sensor_temp_c >= 85
            do action RaiseThermalAlert {
                assign link.temp_c := sensor_temp_c;
                assign alerts_sent := alerts_sent + 1;
                item msg : EvOverTemp {
                    attribute :>> peak_temp = sensor_temp_c;
                }
                send msg via link;
            }
            then OverTempLatched;

        transition t_recover
            first OverTempLatched
            accept EvSample
            if sensor_temp_c < 70
            do action ClearReading {
                assign link.temp_c := sensor_temp_c;
            }
            then Monitoring;
    }

    state def CoolingActuatorFsm {
        out fan_pwm : Integer;
        port link : ThermalLinkPort;
        attribute last_peak_temp : Integer := 0;

        entry; then Standby;

        state Standby;
        state ActiveCooling;

        transition t_engage_fan
            first Standby
            accept alert : EvOverTemp via link
            do action EngageMaxCooling {
                assign last_peak_temp := alert.peak_temp;
                assign fan_pwm := 100;
            }
            then ActiveCooling;

        transition t_standby
            first ActiveCooling
            accept EvCoolComplete
            do action StopFan {
                assign fan_pwm := 0;
            }
            then Standby;
    }
}

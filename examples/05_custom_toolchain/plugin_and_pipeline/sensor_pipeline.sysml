// ============================================================================
// OMG SysML v2.0 State Machine: Industrial Sensor Acquisition Pipeline
// Domain: Embedded Edge Sensing & Digital Signal Processing (DSP)
// Demonstrating: Custom Toolchain Integration, Unix Pipe Filters, and Pass Plugins
// ============================================================================

package SensorToolchain {
    private import ScalarValues::*;

    item def AdcSampleReadyEvent;
    item def FilterCompleteEvent;
    item def PublishAckEvent;
    item def WakeupTimerEvent;
    item def LowBatteryShutdownCmd;

    state def SensorPipelineController {
        // Typed MBSE I/O Parameters with Physical Range Contracts
        in raw_adc_microvolts : Real {
            assert constraint { raw_adc_microvolts >= 0.0 and raw_adc_microvolts <= 3300000.0 }
        }
        in sample_rate_hz : Real {
            assert constraint { sample_rate_hz >= 1.0 and sample_rate_hz <= 50000.0 }
        }
        out filtered_value : Real {
            assert constraint { filtered_value >= 0.0 and filtered_value <= 100.0 }
        }
        out dsp_active : Boolean;

        entry; then SensorSleep;

        state SensorSleep;

        state SensorActive {
            entry; then Sampling;

            state Sampling {
                // @fsm:state time_invariant="10[ms]"
            }

            state Filtering {
                // @fsm:state time_invariant="5[ms]"
            }

            state Publishing;

            transition first Sampling accept AdcSampleReadyEvent
                do action ProcessFirFilterAction
                then Filtering;

            transition first Filtering accept FilterCompleteEvent
                do action PublishMqttTelemetryAction
                then Publishing;

            transition first Publishing accept PublishAckEvent
                then Sampling;
        }

        transition first SensorSleep accept WakeupTimerEvent
            do action PowerUpFrontendAction
            then SensorActive;

        transition first SensorActive accept LowBatteryShutdownCmd
            do action PowerDownFrontendAction
            then SensorSleep;
    }
}

// ============================================================================
// OMG SysML v2 State Machine: Industrial Sensor Acquisition Pipeline
// Domain: Embedded Edge Sensing & Digital Signal Processing (DSP)
// Demonstrating: Custom Toolchain Integration, Unix Pipe Filters, and Pass Plugins
// ============================================================================

package SensorToolchain {
    import ScalarValues::*;

    state def SensorPipelineController {
        // Typed MBSE I/O Parameters with Physical Range Contracts
        in raw_adc_microvolts : Real { assert constraint { self >= 0.0 and self <= 3300000.0; } }
        in sample_rate_hz : Real { assert constraint { self >= 1.0 and self <= 50000.0; } }
        out filtered_value : Real { assert constraint { self >= 0.0 and self <= 100.0; } }
        out dsp_active : Boolean;

        entry; then SensorSleep;

        state SensorSleep;

        state SensorActive {
            entry; then Sampling;

            state Sampling {
                stay duration <= 10[ms];
            }

            state Filtering {
                stay duration <= 5[ms];
            }

            state Publishing;

            transition first Sampling accept AdcSampleReadyEvent
                do ProcessFirFilterAction
                then Filtering;

            transition first Filtering accept FilterCompleteEvent
                do PublishMqttTelemetryAction
                then Publishing;

            transition first Publishing accept PublishAckEvent
                then Sampling;
        }

        transition first SensorSleep accept WakeupTimerEvent
            do PowerUpFrontendAction
            then SensorActive;

        transition first SensorActive accept LowBatteryShutdownCmd
            do PowerDownFrontendAction
            then SensorSleep;
    }
}

// ============================================================================
// OMG SysML v2 State Machine: Industrial Sensor Acquisition Pipeline
// Domain: Embedded Edge Sensing & Digital Signal Processing (DSP)
// Demonstrating: Custom Toolchain Integration, Unix Pipe Filters, and Pass Plugins
// ============================================================================

package SensorToolchain {
    state def SensorPipelineController {
        // Typed MBSE I/O Ports with Physical Range Contracts
        in port raw_adc_microvolts : Real { assert constraint { self >= 0.0 and self <= 3300000.0; } }
        in port sample_rate_hz : Real { assert constraint { self >= 1.0 and self <= 50000.0; } }
        out port filtered_value : Real { assert constraint { self >= 0.0 and self <= 100.0; } }
        out port dsp_active : Boolean;

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

            transition from Sampling accept AdcSampleReadyEvent 
                do ProcessFirFilterAction 
                then Filtering;

            transition from Filtering accept FilterCompleteEvent 
                do PublishMqttTelemetryAction 
                then Publishing;

            transition from Publishing accept PublishAckEvent 
                then Sampling;
        }

        transition from SensorSleep accept WakeupTimerEvent 
            do PowerUpFrontendAction 
            then SensorActive;

        transition from SensorActive accept LowBatteryShutdownCmd 
            do PowerDownFrontendAction 
            then SensorSleep;
    }
}

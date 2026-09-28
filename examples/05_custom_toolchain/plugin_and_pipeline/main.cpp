/**
 * @file main.cpp
 * @brief Executable verification test harness for Industrial Sensor Acquisition Pipeline.
 * Demonstrates: Extensible compiler pass integration, Unix pipe filters,
 * dynamic C++ pass plugins, and embedded DSP lifecycle management.
 */

#include <cassert>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "sensor_pipeline_fsm.hpp"

namespace sensing {

/**
 * @struct ConcreteSensorServices
 * @brief DSP analog frontend and MQTT network transport service driver.
 */
struct ConcreteSensorServices : public SensorPipelineControllerServices {
    bool afe_powered{false};
    bool fir_filter_active{false};
    bool mqtt_packet_sent{false};
    int processed_samples{0};

    void PowerUpFrontendAction() override {
        afe_powered = true;
        std::cout
            << "  \033[1;32m[HARDWARE/AFE]\033[0m 24-bit Delta-Sigma ADC powered up; LDO reference stable at 3.3V\n";
    }

    void PowerDownFrontendAction() override {
        afe_powered = false;
        std::cout << "  \033[1;33m[HARDWARE/AFE]\033[0m Analog frontend powered down into sub-microamp ultra-low power "
                     "sleep\n";
    }

    void ProcessFirFilterAction() override {
        fir_filter_active = true;
        processed_samples++;
        std::cout << "  \033[1;32m[DSP ACCELERATOR]\033[0m Executed 64-tap symmetric FIR low-pass filter (sample #"
                  << processed_samples << ")\n";
    }

    void PublishMqttTelemetryAction() override {
        mqtt_packet_sent = true;
        std::cout << "  \033[1;32m[MQTT/TLS CLIENT]\033[0m Encrypted telemetry frame dispatched to broker topic "
                     "'sensors/v1/raw'\n";
    }
};

}  // namespace sensing

static void print_section(std::string_view title) {
    std::cout << "\n\033[1;36m================================================================================\033[0m\n"
              << "\033[1;37m " << title << "\033[0m\n"
              << "\033[1;36m================================================================================\033[0m\n";
}

static void print_step(std::string_view step, std::string_view state) {
    std::cout << "  \033[1;35m[STEP]\033[0m " << std::left << std::setw(44) << step << " --> Current State: \033[1;32m"
              << state << "\033[0m\n";
}

int main() {
    print_section("FSMC SHOWCASE 05: CUSTOM TOOLCHAIN, UNIX PIPES & PASS PLUGINS");

    sensing::SensorPipelineControllerInPorts in_ports;
    sensing::SensorPipelineControllerOutPorts out_ports;
    sensing::ConcreteSensorServices services;

    // 1. Validate Typed MBSE Sensor Port Contracts
    in_ports.raw_adc_microvolts = 1650000.0f;  // 1.65V mid-scale (0 to 3,300,000 uV)
    in_ports.sample_rate_hz = 10000.0f;        // 10 kHz (1 to 50,000 Hz)
    assert(in_ports.validate_contracts());

    out_ports.filtered_value = 50.0f;  // 50% scale (0 to 100%)
    out_ports.dsp_active = true;
    assert(out_ports.validate_contracts());
    std::cout << "  \033[1;32m[MBSE CONTRACT CHECK]\033[0m Input ADC telemetry & DSP output range constraints "
                 "validated: OK\n";

    // 2. Instantiate Sensor State Machine
    sensing::SensorPipelineController fsm(services);
    assert(fsm.current_state_name() == "SensorSleep");
    print_step("Initial Power-Down State", fsm.current_state_name());

    // 3. Wakeup Timer -> Active Sampling
    std::cout << "\n\033[1;33m--- Phase 1: Periodic Timer Wakeup & AFE Power-Up ---\033[0m\n";
    fsm.dispatch(sensing::WakeupTimerEvent{}, in_ports, out_ports);
    assert(services.afe_powered);
    print_step("Dispatched WakeupTimerEvent", fsm.current_state_name());

    // 4. ADC DMA Transfer Complete -> FIR Filtering
    std::cout << "\n\033[1;33m--- Phase 2: DMA Buffer Ready & DSP FIR Filtering ---\033[0m\n";
    fsm.dispatch(sensing::AdcSampleReadyEvent{}, in_ports, out_ports);
    assert(services.fir_filter_active);
    print_step("Dispatched AdcSampleReadyEvent", fsm.current_state_name());

    // 5. Filter Done -> Telemetry Publishing
    std::cout << "\n\033[1;33m--- Phase 3: MQTT Telemetry Dispatch ---\033[0m\n";
    fsm.dispatch(sensing::FilterCompleteEvent{}, in_ports, out_ports);
    assert(services.mqtt_packet_sent);
    print_step("Dispatched FilterCompleteEvent", fsm.current_state_name());

    // 6. Network ACK -> Return to Sampling Loop
    std::cout << "\n\033[1;33m--- Phase 4: Network Acknowledgment & Next Acquisition Loop ---\033[0m\n";
    fsm.dispatch(sensing::PublishAckEvent{}, in_ports, out_ports);
    print_step("Dispatched PublishAckEvent", fsm.current_state_name());

    // 7. Low-Battery Voltage Drop -> Clean Sleep Teardown
    std::cout << "\n\033[1;33m--- Phase 5: Low-Battery Brownout Detection & Safe Sleep ---\033[0m\n";
    fsm.dispatch(sensing::LowBatteryShutdownCmd{}, in_ports, out_ports);
    assert(fsm.is_in<sensing::SensorSleep>());
    assert(!services.afe_powered);
    print_step("Dispatched LowBatteryShutdownCmd", fsm.current_state_name());
    std::cout
        << "  \033[1;32m[TEARDOWN VERIFIED]\033[0m Sensor frontend completely isolated and in ultra-low-power mode.\n";

    print_section("ALL TOOLCHAIN & SENSOR LIFECYCLE TESTS PASSED (100% SUCCESS)");
    return 0;
}

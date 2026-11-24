/**
 * @file main.cpp
 * @brief Showcase 07: Modular MBSE Toolchain — ir-json ingestion, split *_ports.hpp/*_fsm.hpp,
 * zero-vtable ServicesContract, C99 zero-alloc ABI, and cross-artifact SHA-256 attestation.
 */

#include <cassert>
#include <iostream>
#include <string>

#include "thermal_controller_fsm.hpp"

void hal_sample_temperature(thermal::ThermalControllerInPorts& in_ports, int32_t measured_c);
extern "C" int run_c99_thermal_client(const char* expected_sha256);

namespace {

/// Pure zero-vtable service implementation (e.g. external control law or HAL)
struct ControlLawService {
    int32_t gain = 2;

    [[nodiscard]] int32_t compute_pwm(int32_t temp_c) const noexcept {
        const int32_t duty = (temp_c - 20) * gain;
        if (duty < 0) {
            return 0;
        }
        return duty > 100 ? 100 : duty;
    }
};

}  // namespace

int main() {
    static_assert(thermal::ThermalControllerIrSha256.size() == 64);

    // 1. C++17 zero-vtable execution with split HAL driver + ServicesContract
    thermal::ThermalControllerRegisters reg{};
    ControlLawService pid{};
    thermal::ThermalControllerWithServices<ControlLawService> sm(reg, pid);

    thermal::ThermalControllerInPorts in{};
    thermal::ThermalControllerOutPorts out{};

    auto r1 = sm.dispatch(thermal::Enable{}, in, out);
    if (!r1.is_success() || !sm.is_in<thermal::Regulating>()) {
        return 1;
    }

    hal_sample_temperature(in, 55);
    auto r2 = sm.dispatch(thermal::Sample{}, in, out);
    if (!r2.is_success() || out.pwm_duty != 70) {
        std::cerr << "Unexpected C++ pwm_duty: " << out.pwm_duty << "\n";
        return 1;
    }

    // 2. Pure C99 client execution via zero-alloc C ABI + SHA-256 attestation
    const std::string sha_str(thermal::ThermalControllerIrSha256);
    const int c_duty = run_c99_thermal_client(sha_str.c_str());
    if (c_duty != out.pwm_duty) {
        std::cerr << "C99 ABI mismatch: expected " << out.pwm_duty << ", got " << c_duty << "\n";
        return 1;
    }

    std::cout << "[Showcase 07] Modular Toolchain & C99 ABI verified (pwm_duty=" << out.pwm_duty
              << ", ir_sha256=" << sha_str.substr(0, 12) << "...)\n";
    return 0;
}

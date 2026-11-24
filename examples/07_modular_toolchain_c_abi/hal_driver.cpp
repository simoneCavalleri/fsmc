/**
 * @file hal_driver.cpp
 * @brief Demonstrates HAL/driver isolation: includes ONLY the split *_ports.hpp header (zero fsm.hpp dependency).
 */

#include "thermal_controller_ports.hpp"

void hal_sample_temperature(thermal::ThermalControllerInPorts& in_ports, int32_t measured_c) {
    in_ports.temp_c = measured_c;
}

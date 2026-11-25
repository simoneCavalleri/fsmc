/**
 * @file main.c
 * @brief Pure-C99 deterministic multi-FSM system loop (zero C++ runtime, zero heap).
 *
 * Connects two OMG SysML v2.0 state machines compiled from a single multi-file
 * workspace (`thermal_subsystem.sysml` + `shared_protocol.sysml`):
 *   - SensorMonitorFsm   (`--state-def SensorMonitorFsm --target c99`)
 *   - CoolingActuatorFsm (`--state-def CoolingActuatorFsm --target c99`)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cooling_actuator.h"
#include "sensor_monitor.h"

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "CHECK FAILED at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                                  \
        }                                                                              \
    } while (0)

int main(void) {
    sensor_monitor_fsm_fsm_t sensor;
    cooling_actuator_fsm_fsm_t cooler;

    sensor_monitor_fsm_init(&sensor);
    cooling_actuator_fsm_init(&cooler);

    CHECK(sensor_monitor_fsm_current_state(&sensor) == SENSOR_MONITOR_FSM_STATE_MONITORING);
    CHECK(cooling_actuator_fsm_current_state(&cooler) == COOLING_ACTUATOR_FSM_STATE_STANDBY);

    /* Cycle 1: Nominal temperature (60 C) -> no overtemp alert emitted */
    {
        sensor_monitor_fsm_in_ports_t s_in;
        sensor_monitor_fsm_out_ports_t s_out;
        memset(&s_in, 0, sizeof(s_in));
        memset(&s_out, 0, sizeof(s_out));
        s_in.sensor_temp_c = 60;

        CHECK(sensor_monitor_fsm_dispatch_ev_sample(&sensor, &s_in, &s_out) ==
              SENSOR_MONITOR_FSM_STATUS_GUARD_REJECTED);
        CHECK(s_out.outbox_count == 0U);
        CHECK(cooling_actuator_fsm_current_state(&cooler) == COOLING_ACTUATOR_FSM_STATE_STANDBY);
    }

    /* Cycle 2: Over-temperature (92 C) -> Sensor emits EvOverTemp(92) via link -> routed to Cooler */
    {
        sensor_monitor_fsm_in_ports_t s_in;
        sensor_monitor_fsm_out_ports_t s_out;
        cooling_actuator_fsm_in_ports_t c_in;
        cooling_actuator_fsm_out_ports_t c_out;
        size_t i;

        memset(&s_in, 0, sizeof(s_in));
        memset(&s_out, 0, sizeof(s_out));
        memset(&c_in, 0, sizeof(c_in));
        memset(&c_out, 0, sizeof(c_out));

        s_in.sensor_temp_c = 92;
        CHECK(sensor_monitor_fsm_dispatch_ev_sample(&sensor, &s_in, &s_out) == SENSOR_MONITOR_FSM_STATUS_OK);
        CHECK(sensor_monitor_fsm_current_state(&sensor) == SENSOR_MONITOR_FSM_STATE_OVER_TEMP_LATCHED);
        CHECK(sensor.regs.alerts_sent == 1U);
        CHECK(s_out.temp_c == 92);
        CHECK(s_out.outbox_count == 1U);
        CHECK(s_out.outbox[0].tag == SENSOR_MONITOR_FSM_OUT_EV_OVER_TEMP);
        CHECK(strcmp(s_out.outbox[0].port, "link") == 0);
        CHECK(s_out.outbox[0].ev_over_temp.peak_temp == 92);

        /* Deterministic zero-heap bus routing: forward Sensor outbox -> Cooler */
        c_in.temp_c = s_out.temp_c;
        for (i = 0U; i < s_out.outbox_count; ++i) {
            if (s_out.outbox[i].tag == SENSOR_MONITOR_FSM_OUT_EV_OVER_TEMP) {
                CHECK(cooling_actuator_fsm_dispatch_ev_over_temp(&cooler, s_out.outbox[i].ev_over_temp.peak_temp, &c_in,
                                                                 &c_out) == COOLING_ACTUATOR_FSM_STATUS_OK);
            }
        }

        CHECK(cooling_actuator_fsm_current_state(&cooler) == COOLING_ACTUATOR_FSM_STATE_ACTIVE_COOLING);
        CHECK(cooler.regs.last_peak_temp == 92);
        CHECK(c_out.fan_pwm == 100U);
    }

    /* Cycle 3: Temperature cools down (65 C) -> Sensor returns to Monitoring, Cooler returns to Standby */
    {
        sensor_monitor_fsm_in_ports_t s_in;
        sensor_monitor_fsm_out_ports_t s_out;
        cooling_actuator_fsm_in_ports_t c_in;
        cooling_actuator_fsm_out_ports_t c_out;

        memset(&s_in, 0, sizeof(s_in));
        memset(&s_out, 0, sizeof(s_out));
        memset(&c_in, 0, sizeof(c_in));
        memset(&c_out, 0, sizeof(c_out));

        s_in.sensor_temp_c = 65;
        CHECK(sensor_monitor_fsm_dispatch_ev_sample(&sensor, &s_in, &s_out) == SENSOR_MONITOR_FSM_STATUS_OK);
        CHECK(sensor_monitor_fsm_current_state(&sensor) == SENSOR_MONITOR_FSM_STATE_MONITORING);
        CHECK(s_out.outbox_count == 0U);

        CHECK(cooling_actuator_fsm_dispatch_ev_cool_complete(&cooler, &c_in, &c_out) == COOLING_ACTUATOR_FSM_STATUS_OK);
        CHECK(cooling_actuator_fsm_current_state(&cooler) == COOLING_ACTUATOR_FSM_STATE_STANDBY);
        CHECK(c_out.fan_pwm == 0U);
    }

    printf("[OK] Multi-FSM SysML v2.0 thermal subsystem verified (Pure-C99, zero heap, conjugated ~PortDef).\n");
    return 0;
}

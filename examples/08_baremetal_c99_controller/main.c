/**
 * @file main.c
 * @brief Showcase 08: Pure-C99 Bare-Metal Controller compiled with ONLY a C99 compiler (zero C++ runtime).
 */

#include <stdio.h>
#include <string.h>

#include "watchdog_controller.h"

typedef struct {
    uint32_t gain;
    uint32_t calls;
} pid_ctx_t;

static uint32_t hal_compute_duty(void* user_data, uint32_t temp) {
    pid_ctx_t* ctx = (pid_ctx_t*)user_data;
    uint32_t duty;
    ctx->calls += 1U;
    if (temp <= 20U) {
        return 0U;
    }
    duty = (temp - 20U) * ctx->gain;
    if (duty > 100U) {
        return 100U;
    }
    return duty;
}

int main(void) {
    pid_ctx_t ctx = {2, 0};
    watchdog_controller_services_t srv = {&ctx, hal_compute_duty};
    watchdog_controller_fsm_t fsm;
    watchdog_controller_fsm_t restored;
    watchdog_controller_in_ports_t in_p;
    watchdog_controller_out_ports_t out_p;
    uint8_t snap[128];
    size_t written = 0U;
    size_t read_sz = 0U;

    memset(&in_p, 0, sizeof(in_p));
    memset(&out_p, 0, sizeof(out_p));

    watchdog_controller_init_with_services(&fsm, &srv);
    if (watchdog_controller_current_state(&fsm) != WATCHDOG_CONTROLLER_STATE_STANDBY) {
        return 1;
    }
    if (strlen(watchdog_controller_ir_sha256()) != 64U) {
        return 2;
    }

    /* Guard rejects arming when temperature >= 90 */
    in_p.temp_c = 95;
    if (watchdog_controller_dispatch_ev_arm(&fsm, &in_p, &out_p) != WATCHDOG_CONTROLLER_STATUS_GUARD_REJECTED) {
        return 3;
    }

    /* Arm with nominal temperature */
    in_p.temp_c = 55;
    if (watchdog_controller_dispatch_ev_arm(&fsm, &in_p, &out_p) != WATCHDOG_CONTROLLER_STATUS_OK ||
        watchdog_controller_current_state(&fsm) != WATCHDOG_CONTROLLER_STATE_ACTIVE) {
        return 4;
    }

    /* Sample invokes pure C99 service callback and updates register */
    if (watchdog_controller_dispatch_ev_sample(&fsm, &in_p, &out_p) != WATCHDOG_CONTROLLER_STATUS_OK ||
        out_p.pwm_duty != 70 || ctx.calls != 1 || fsm.regs.sample_count != 1) {
        return 5;
    }

    /* Advance 60ms (still Active) and take a binary snapshot */
    if (watchdog_controller_tick(&fsm, 60U, &in_p, &out_p) != WATCHDOG_CONTROLLER_STATUS_IGNORED ||
        watchdog_controller_current_state(&fsm) != WATCHDOG_CONTROLLER_STATE_ACTIVE) {
        return 6;
    }
    if (!watchdog_controller_save_snapshot(&fsm, snap, sizeof(snap), &written) || written == 0U) {
        return 7;
    }

    /* Advance 50ms -> watchdog timeout (60 + 50 >= 100ms) -> SafeShutdown */
    if (watchdog_controller_tick(&fsm, 50U, &in_p, &out_p) != WATCHDOG_CONTROLLER_STATUS_OK ||
        watchdog_controller_current_state(&fsm) != WATCHDOG_CONTROLLER_STATE_SAFE_SHUTDOWN || out_p.pwm_duty != 0) {
        return 8;
    }

    /* Restore snapshot into fresh FSM -> back in Active with 60ms elapsed and 40ms remaining */
    watchdog_controller_init_with_services(&restored, &srv);
    if (!watchdog_controller_restore_snapshot(&restored, snap, written, &read_sz) || read_sz != written ||
        watchdog_controller_current_state(&restored) != WATCHDOG_CONTROLLER_STATE_ACTIVE ||
        restored.regs.sample_count != 1) {
        return 9;
    }
    if (watchdog_controller_tick(&restored, 40U, &in_p, &out_p) != WATCHDOG_CONTROLLER_STATUS_OK ||
        watchdog_controller_current_state(&restored) != WATCHDOG_CONTROLLER_STATE_SAFE_SHUTDOWN) {
        return 10;
    }

    watchdog_controller_destroy(&restored);
    watchdog_controller_destroy(&fsm);
    printf("[Showcase 08] Bare-Metal Pure-C99 verified (pwm_duty=70, watchdog=100ms, ir_sha256=%.12s...)\n",
           watchdog_controller_ir_sha256());
    return 0;
}

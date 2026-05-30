#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Begin a manual session. Setpoint in °C, duration in seconds. Switches
// op_mode = MANUAL, run_state = RUNNING, energises fans, gates PID on.
esp_err_t modo_manual_start(float setpoint_c, uint32_t duration_s);

// Soft pause: PID setpoint goes to 0, SSR_DRV stays off, SSR_FAN stays off.
// Countdown is frozen. run_state = PAUSED.
esp_err_t modo_manual_pause(void);
esp_err_t modo_manual_resume(void);

// Hard stop. Frees the session, run_state = IDLE.
esp_err_t modo_manual_stop(void);

// Called from control_task each cycle to tick session elapsed/remaining and
// detect completion. dt_s is the control period in seconds.
void      modo_manual_tick(float dt_s);

// True while a manual session is RUNNING or PAUSED.
bool      modo_manual_active(void);

#ifdef __cplusplus
}
#endif

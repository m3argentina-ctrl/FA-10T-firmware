#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float kp;
    float ki;
    float kd;

    float out_min;
    float out_max;

    float d_filter_alpha;   // 0..1, low-pass on derivative (0 = no filter, 1 = freeze)
    bool  derivative_on_measurement; // true to avoid derivative kick on setpoint changes
} pid_params_t;

typedef struct {
    pid_params_t params;

    float integral;
    float prev_input;
    float prev_error;
    float prev_d_filtered;

    float setpoint;
    float output;

    bool  first_run;
    bool  saturated;
} pid_t;

void  pid_init(pid_t *pid, const pid_params_t *params);
void  pid_set_params(pid_t *pid, const pid_params_t *params);
void  pid_set_setpoint(pid_t *pid, float setpoint);
void  pid_set_limits(pid_t *pid, float min, float max);
void  pid_reset(pid_t *pid);

// dt in seconds. Returns the controller output, clamped to [out_min, out_max].
float pid_compute(pid_t *pid, float input, float dt);

#ifdef __cplusplus
}
#endif

#include "pid_controller.h"
#include <string.h>

static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void pid_init(pid_t *pid, const pid_params_t *params)
{
    memset(pid, 0, sizeof(*pid));
    pid->params = *params;
    pid->first_run = true;
}

void pid_set_params(pid_t *pid, const pid_params_t *params)
{
    pid->params = *params;
}

void pid_set_setpoint(pid_t *pid, float sp)
{
    pid->setpoint = sp;
}

void pid_set_limits(pid_t *pid, float min, float max)
{
    pid->params.out_min = min;
    pid->params.out_max = max;
    pid->integral = clampf(pid->integral, min, max);
    pid->output   = clampf(pid->output,   min, max);
}

void pid_reset(pid_t *pid)
{
    pid->integral = 0.0f;
    pid->prev_input = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev_d_filtered = 0.0f;
    pid->output = 0.0f;
    pid->first_run = true;
    pid->saturated = false;
}

float pid_compute(pid_t *pid, float input, float dt)
{
    if (dt <= 0.0f) return pid->output;

    const float error = pid->setpoint - input;

    // ------ Proportional ------
    const float P = pid->params.kp * error;

    // ------ Derivative ------
    float d_raw;
    if (pid->first_run) {
        d_raw = 0.0f;
    } else if (pid->params.derivative_on_measurement) {
        // d/dt of measurement (negated). Avoids derivative kick on setpoint changes.
        d_raw = -(input - pid->prev_input) / dt;
    } else {
        d_raw = (error - pid->prev_error) / dt;
    }
    const float alpha = pid->params.d_filter_alpha;
    const float d_filt = (alpha > 0.0f && !pid->first_run)
        ? (alpha * pid->prev_d_filtered + (1.0f - alpha) * d_raw)
        : d_raw;
    const float D = pid->params.kd * d_filt;

    // ------ Integral with conditional anti-windup ------
    // Only integrate when not pushing further into saturation in the same direction
    bool windup_block = pid->saturated &&
        ((pid->output >= pid->params.out_max && error > 0.0f) ||
         (pid->output <= pid->params.out_min && error < 0.0f));

    if (!windup_block) {
        pid->integral += pid->params.ki * error * dt;
        // Conditional anti-windup above keeps the integral bounded; clamping
        // it to the output range here would over-restrict it for setups where
        // I must compensate for a small Kp.
    }
    const float I = pid->integral;

    float out = P + I + D;
    const float clamped = clampf(out, pid->params.out_min, pid->params.out_max);
    pid->saturated = (clamped != out);
    pid->output    = clamped;

    pid->prev_input = input;
    pid->prev_error = error;
    pid->prev_d_filtered = d_filt;
    pid->first_run = false;

    return pid->output;
}

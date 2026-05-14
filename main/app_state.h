#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "nvs_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Message from sensor_task → control_task
typedef struct {
    float    temperature;     // °C, after calibration
    float    raw_temperature; // °C, pre-calibration
    bool     fault;
    uint8_t  fault_status;
    uint64_t timestamp_us;
} sensor_sample_t;

// Snapshot shared with the UI and watchdog. Read under app_state_lock().
typedef struct {
    sensor_sample_t last_sample;
    float           setpoint;
    float           pid_output;     // 0..1
    float           ssr_duty;       // 0..1
    uint32_t        safety_faults;  // bitmask
    uint32_t        uptime_s;
    bool            running;
} app_state_t;

void              app_state_init(void);
QueueHandle_t     app_state_sensor_queue(void);
void              app_state_lock(void);
void              app_state_unlock(void);
app_state_t      *app_state_get(void);   // call between lock/unlock
const fa10t_config_t *app_state_config(void);
void              app_state_set_config(const fa10t_config_t *cfg);

#ifdef __cplusplus
}
#endif

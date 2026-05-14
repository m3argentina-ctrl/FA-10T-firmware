#pragma once

#include "max31865.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

void          sensor_task_start(max31865_handle_t rtd);
TaskHandle_t  sensor_task_handle(void);

#ifdef __cplusplus
}
#endif

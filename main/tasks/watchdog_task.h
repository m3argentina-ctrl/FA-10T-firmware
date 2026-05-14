#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

void          watchdog_task_start(void);
TaskHandle_t  watchdog_task_handle(void);

#ifdef __cplusplus
}
#endif

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

void          control_task_start(void);
TaskHandle_t  control_task_handle(void);

#ifdef __cplusplus
}
#endif

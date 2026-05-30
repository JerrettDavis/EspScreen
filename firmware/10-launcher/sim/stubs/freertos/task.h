/**
 * freertos/task.h — host stub. xTaskCreatePinnedToCore reports success but does
 * NOT run the task fn, so claude_widget's network poll never executes in the harness.
 */
#pragma once
#include "FreeRTOS.h"

typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);

static inline BaseType_t xTaskCreatePinnedToCore(
        TaskFunction_t /*fn*/, const char* /*name*/, unsigned /*stack*/,
        void* /*arg*/, UBaseType_t /*prio*/, TaskHandle_t* handle, BaseType_t /*core*/) {
    if (handle) *handle = (TaskHandle_t)0;
    return pdPASS;   /* claim success but never invoke fn */
}

static inline void vTaskDelete(TaskHandle_t /*h*/) {}
static inline void vTaskDelay(TickType_t /*ticks*/) {}

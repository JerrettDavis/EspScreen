/**
 * freertos/queue.h — host stub. Queue ops are no-ops; xQueueReceive always
 * reports empty (pdFALSE) so the apply path drains nothing.
 */
#pragma once
#include "FreeRTOS.h"

typedef void* QueueHandle_t;

/* Non-null sentinel so the lazy-init "if (!s_result_q)" check passes. */
static inline QueueHandle_t xQueueCreate(UBaseType_t /*len*/, UBaseType_t /*item_sz*/) {
    return (QueueHandle_t)1;
}
static inline BaseType_t xQueueOverwrite(QueueHandle_t /*q*/, const void* /*item*/) {
    return pdPASS;
}
static inline BaseType_t xQueueReceive(QueueHandle_t /*q*/, void* /*buf*/, TickType_t /*wait*/) {
    return pdFALSE;   /* nothing to drain */
}
static inline BaseType_t xQueueReset(QueueHandle_t /*q*/) { return pdPASS; }
static inline void       vQueueDelete(QueueHandle_t /*q*/) {}

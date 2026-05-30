/**
 * freertos/FreeRTOS.h — host stub. Just the base typedefs/macros claude_widget
 * needs to compile. No scheduler; tasks are never actually run by the harness.
 */
#pragma once

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef unsigned int TickType_t;

#ifndef pdTRUE
#define pdTRUE  1
#endif
#ifndef pdFALSE
#define pdFALSE 0
#endif
#ifndef pdPASS
#define pdPASS  1
#endif
#ifndef pdFAIL
#define pdFAIL  0
#endif

#ifndef portMAX_DELAY
#define portMAX_DELAY 0xffffffffU
#endif

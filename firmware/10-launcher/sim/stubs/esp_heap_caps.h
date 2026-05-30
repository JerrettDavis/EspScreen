/**
 * esp_heap_caps.h — host stub. Provides esp_get_free_heap_size() (used by
 * about/claude_widget for display) and no-op heap_caps_* helpers.
 */
#pragma once
#include <cstddef>
#include <cstdint>

#ifndef MALLOC_CAP_8BIT
#define MALLOC_CAP_8BIT     (1 << 2)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_SPIRAM   (1 << 10)
#define MALLOC_CAP_DEFAULT  0
#endif

/* esp_get_free_heap_size is also declared in the Arduino.h stub (it's exposed
 * transitively there on-device). Guard to avoid a redefinition when both are
 * included in the same TU (claude_widget includes Arduino.h AND esp_heap_caps.h). */
#ifndef ESPSCREEN_SIM_HAVE_FREE_HEAP
#define ESPSCREEN_SIM_HAVE_FREE_HEAP
static inline uint32_t esp_get_free_heap_size(void) { return 70000; }
#endif
static inline size_t   heap_caps_get_free_size(uint32_t /*caps*/) { return 70000; }
static inline size_t   heap_caps_get_largest_free_block(uint32_t /*caps*/) { return 40000; }

#pragma once
#include <stdint.h>
#include <Arduino.h>

/**
 * NVS store — thin wrapper around ESP32 Preferences library.
 * Namespace-scoped: open/put/get/close pattern.
 * Key strings must be ≤ 15 chars (ESP NVS limit).
 */
namespace nvs_store {
    /* int32 */
    void put_i32(const char* ns, const char* key, int32_t val);
    int32_t get_i32(const char* ns, const char* key, int32_t default_val = 0);

    /* uint8 */
    void put_u8(const char* ns, const char* key, uint8_t val);
    uint8_t get_u8(const char* ns, const char* key, uint8_t default_val = 0);

    /* string (max 63 chars) */
    void put_str(const char* ns, const char* key, const char* val);
    String get_str(const char* ns, const char* key, const char* default_val = "");
}

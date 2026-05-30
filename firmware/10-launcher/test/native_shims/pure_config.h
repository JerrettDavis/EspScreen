/**
 * pure_config.h — Portable shim for config clamping logic.
 * No Arduino, no LittleFS, no ArduinoJson dependencies.
 * Mirrors config::MirrorCfg and the clamp_mirror() logic from src/os/config.cpp.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

struct MirrorCfg {
    bool enabled;
    int  interval_ms;
    int  out_width;
    int  out_height;
};

/**
 * Clamp a MirrorCfg to valid ranges in-place.
 * Ranges:  interval_ms ∈ [100, 2000]
 *          out_width   ∈ [16,  80]
 *          out_height  ∈ [24,  120]
 */
inline void clamp_mirror(MirrorCfg& m) {
    if (m.interval_ms < 100)  m.interval_ms = 100;
    if (m.interval_ms > 2000) m.interval_ms = 2000;
    if (m.out_width   < 16)   m.out_width   = 16;
    if (m.out_width   > 80)   m.out_width   = 80;
    if (m.out_height  < 24)   m.out_height  = 24;
    if (m.out_height  > 120)  m.out_height  = 120;
}

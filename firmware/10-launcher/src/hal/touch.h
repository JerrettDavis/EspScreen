#pragma once
#include <stdint.h>

namespace touch {
    /**
     * Register LVGL touch input device using TFT_eSPI built-in mapping.
     * Uses hardcoded Phase 0 factory cal {275, 3620, 264, 3532, 1}.
     * No NVS reads, no custom affine math, no cal prompts.
     * Called from setup() after display::init().
     */
    void init();

    /**
     * Toggle raw touch diagnostic logging to Serial.
     * When enabled, every touch_read_cb call logs mapped coordinates at ≤10 Hz.
     * Activated via 'tdbg' serial command.
     */
    void set_debug(bool enabled);
    bool get_debug();
}

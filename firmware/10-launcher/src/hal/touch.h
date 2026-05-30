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

    /**
     * Inject a synthetic two-phase tap at (x, y) in LVGL 320×480 coordinates.
     * Phase 1 (next touch_read_cb cycle): emits PRESSED at (x,y).
     * Phase 2 (cycle after that):         emits RELEASED at the same (x,y).
     * Both phases use the same coordinates so LVGL registers a clean tap, not
     * a drag.  If a previous inject is still in-progress it is silently dropped
     * (no queue).
     */
    void inject(int16_t x, int16_t y);
}

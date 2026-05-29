#pragma once
#include <stdint.h>

namespace touch {
    /**
     * Register LVGL touch input device and load/run calibration.
     * Called from setup() after display::init().
     * On first boot (NVS calibrated==0) → runs 4-corner calibration sequence.
     */
    void init();

    /**
     * Run the interactive 4-corner calibration overlay (legacy blocking version).
     * Blocks until user taps all 4 corners. Saves results to NVS.
     */
    void run_calibration();

    /**
     * Goal 1: Toggle raw touch diagnostic logging to Serial.
     * When enabled, every touch_read_cb call logs raw and mapped coordinates
     * at ≤10 Hz. Thread-safe (single-threaded Arduino main loop).
     */
    void set_debug(bool enabled);
    bool get_debug();

    /**
     * Apply a new set of calibration values immediately (hot-reload).
     * Called by calibrate.cpp after the hold-to-settle sequence completes.
     * Values must already be validated and saved to NVS before calling this.
     */
    void apply_cal(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max);
}

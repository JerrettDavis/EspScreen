#pragma once

namespace touch {
    /**
     * Register LVGL touch input device and load/run calibration.
     * Called from setup() after display::init().
     * On first boot (NVS calibrated==0) → runs 4-corner calibration sequence.
     */
    void init();

    /**
     * Run the interactive 4-corner calibration overlay.
     * Blocks until user taps all 4 corners. Saves results to NVS.
     */
    void run_calibration();
}

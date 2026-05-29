#pragma once

namespace display {
    /**
     * Initialise LVGL 9, create the TFT_eSPI display driver, and enable backlight.
     * Must be called once from setup() before any lv_* calls.
     */
    void init();
}

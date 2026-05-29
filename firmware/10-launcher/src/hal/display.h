#pragma once
#include <TFT_eSPI.h>

namespace display {
    /**
     * Initialise LVGL 9, create a custom TFT_eSPI display driver with a single
     * shared TFT_eSPI instance, and enable backlight.
     * Must be called once from setup() before any lv_* calls.
     */
    void init();

    /**
     * Return the single shared TFT_eSPI instance owned by display::init().
     * touch.cpp uses this for getTouch() to avoid a second SPI instance.
     * Returns nullptr before display::init() is called.
     */
    TFT_eSPI* get_tft();
}

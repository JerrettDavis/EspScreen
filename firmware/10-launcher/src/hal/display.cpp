#include "display.h"
#include <Arduino.h>
#include <lvgl.h>

/* Tick callback — returns millis() so LVGL knows elapsed time */
static uint32_t tick_get_cb() { return (uint32_t)millis(); }

/* ── Draw buffer ─────────────────────────────────────────────────────
 * 320 * 20 pixels × 2 bytes = 12.8 KB. LVGL flushes line-by-line so
 * this is plenty. Increase to 320*40 if tearing is visible at 60 fps.
 */
static lv_color_t draw_buf[320 * 20];

namespace display {

void init() {
    /* 1. Initialise LVGL core */
    lv_init();

    /* 1a. Register Arduino millis() as the LVGL tick source.
     *     In LVGL 9.5 LV_TICK_CUSTOM doesn't exist in lv_conf.h;
     *     use lv_tick_set_cb() instead to avoid the lv_tick_inc warning.
     */
    lv_tick_set_cb(tick_get_cb);

    /* 2. Create display via the TFT_eSPI first-class driver.
     *    lv_tft_espi_create() internally:
     *      - instantiates TFT_eSPI, calls tft.begin()
     *      - registers a flush_cb that calls tft.pushImage()
     *      - calls lv_display_set_buffers() with the buffer we supply
     *    This is the LVGL 9 preferred path; no manual flush_cb needed.
     */
    lv_display_t *disp = lv_tft_espi_create(320, 480, draw_buf, sizeof(draw_buf));
    (void)disp;  // held internally by LVGL; we don't need to keep the pointer

    /* 3. Enable backlight (active HIGH per platformio.ini) */
    pinMode(27, OUTPUT);
    digitalWrite(27, HIGH);
}

} // namespace display

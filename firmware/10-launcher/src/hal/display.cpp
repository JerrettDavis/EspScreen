#include "display.h"
#include "../os/screen_mirror.h"
#ifdef FB_STREAM
#include "../os/fb_stream.h"
#endif
#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>

/**
 * display.cpp — single TFT_eSPI instance owned here.
 *
 * Instead of lv_tft_espi_create() (which heap-allocates a private TFT_eSPI*
 * with no way to retrieve it), we:
 *   1. Declare one global TFT_eSPI here.
 *   2. Call tft.begin() / tft.setRotation() ourselves.
 *   3. Register a custom flush_cb that calls tft.startWrite() / pushPixels /
 *      endWrite() — identical to what lv_tft_espi.cpp does internally.
 *   4. Expose get_tft() so touch.cpp can share the same instance for
 *      getTouch() calls without ever calling begin() a second time.
 *
 * This eliminates the "two TFT_eSPI instances on one SPI bus" fragility that
 * the Phase 1 diagnostician flagged.
 */

/* ── Tick callback ───────────────────────────────────────────────── */
static uint32_t tick_get_cb() { return (uint32_t)millis(); }

/* ── Draw buffer ─────────────────────────────────────────────────────
 * 320 * 20 pixels × 2 bytes = 12.8 KB.
 */
static lv_color_t draw_buf[320 * 20];

/* ── Single shared TFT_eSPI instance ────────────────────────────── */
static TFT_eSPI s_tft_global;

/* ── LVGL flush callback ─────────────────────────────────────────── */
static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    s_tft_global.startWrite();
    s_tft_global.setAddrWindow(area->x1, area->y1, w, h);
    s_tft_global.pushColors((uint16_t*)px_map, w * h, true);
    s_tft_global.endWrite();

    if (screen_mirror::enabled()) screen_mirror::on_flush(area, px_map);

#ifdef FB_STREAM
    /* Live framebuffer tap (debug builds only): streams the raw RGB565-LE
     * px_map — what LVGL drew, BEFORE the hardware byte-swap above. */
    fb_stream::on_flush(area, px_map);
#endif

    lv_display_flush_ready(disp);
}

namespace display {

void init() {
    /* 1. Initialise LVGL core */
    lv_init();

    /* 1a. Register Arduino millis() as the LVGL tick source */
    lv_tick_set_cb(tick_get_cb);

    /* 2. Init our single shared TFT_eSPI instance */
    s_tft_global.begin();
    s_tft_global.setRotation(0);

    /* 3. Create LVGL display and wire up our custom flush_cb */
    lv_display_t* disp = lv_display_create(320, 480);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, draw_buf, nullptr, sizeof(draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* 4. Enable backlight (active HIGH per platformio.ini) */
    pinMode(27, OUTPUT);
    digitalWrite(27, HIGH);
}

TFT_eSPI* get_tft() {
    return &s_tft_global;
}

} // namespace display

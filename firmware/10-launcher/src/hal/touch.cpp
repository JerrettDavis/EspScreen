#include "touch.h"
#include "display.h"
#include "../os/logger.h"
#include <TFT_eSPI.h>
#include <lvgl.h>

/**
 * touch.cpp — Phase 0 TFT_eSPI built-in touch pipeline.
 *
 * Uses tft->setTouch(calData) with hardcoded Setup77 factory cal values,
 * then tft->getTouch(&x, &y) in the LVGL read callback.
 * TFT_eSPI handles all pressure thresholding and coordinate mapping internally.
 *
 * No NVS reads, no custom affine math, no IRQ gating, no axis-swap detection.
 * This is the exact pattern that worked in Phase 0.
 *
 * Goal 3: Uses display::get_tft() — no local TFT_eSPI instance.
 */

/* ── Phase 0 factory cal values (TFT_eSPI Setup77, proven to work) ── */
/* calData[4] bitfield: bit0=swap X/Y, bit1=invert mapped X, bit2=invert mapped Y
 * 1 = swap only (original Phase 0)
 * 3 = swap + invert X — corrects observed 90° axis swap + X inversion:
 *     tap UP→dot RIGHT, tap DOWN→dot LEFT, tap LEFT→dot UP, tap RIGHT→dot DOWN
 */
static const uint16_t kCalData[5] = { 275, 3620, 264, 3532, 4 };

/* ── Debug logging flag (toggled by 'tdbg' serial command) ──────── */
static bool    s_debug_touch  = false;
static uint32_t s_last_dbg_ms = 0;

/* ── Inject state (two-phase synthetic tap: 1=PRESSED, 2=RELEASED) ─ */
static int8_t  s_inject_phase = 0;   // 0=idle, 1=emit PRESSED, 2=emit RELEASED
static int16_t s_inject_x     = 0;
static int16_t s_inject_y     = 0;

/* ── LVGL indev read callback ────────────────────────────────────── */
static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    /* ── Two-phase injected tap takes priority ───────────────────── */
    if (s_inject_phase > 0) {
        data->point.x = s_inject_x;
        data->point.y = s_inject_y;
        if (s_inject_phase == 1) {
            data->state    = LV_INDEV_STATE_PRESSED;
            s_inject_phase = 2;
        } else {
            data->state    = LV_INDEV_STATE_RELEASED;
            s_inject_phase = 0;
        }
        return;
    }

    TFT_eSPI* tft = display::get_tft();
    uint16_t x, y;
    if (tft->getTouch(&x, &y)) {
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = (lv_coord_t)x;
        data->point.y = (lv_coord_t)y;
        if (s_debug_touch && millis() - s_last_dbg_ms > 100) {
            s_last_dbg_ms = millis();
            LOG_I("touch", "mapped=(%u,%u)", x, y);
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

namespace touch {

void set_debug(bool enabled) {
    s_debug_touch = enabled;
    Serial.printf("[touch] Debug logging %s\n", enabled ? "ENABLED" : "DISABLED");
}

bool get_debug() {
    return s_debug_touch;
}

void inject(int16_t x, int16_t y) {
    if (s_inject_phase != 0) {
        LOG_W("touch", "inject(%d,%d) dropped — previous inject still in progress (phase=%d)", x, y, s_inject_phase);
        return;
    }
    s_inject_x     = x;
    s_inject_y     = y;
    s_inject_phase = 1;
}

void init() {
    TFT_eSPI* tft = display::get_tft();

    /* Apply Phase 0 factory cal — TFT_eSPI maps raw ADC → rotation-aware (x,y) */
    tft->setTouch(const_cast<uint16_t*>(kCalData));

    LOG_I("touch", "init: using TFT_eSPI built-in mapping, cal={275,3620,264,3532,4}");
    Serial.println("[touch] init: using TFT_eSPI built-in mapping, cal={275,3620,264,3532,4}");

    /* Register LVGL input device */
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
}

} // namespace touch

#include "touch.h"
#include "display.h"
#include "../os/nvs_store.h"
#include "../os/logger.h"
#include <TFT_eSPI.h>
#include <lvgl.h>

/**
 * Touch driver — XPT2046 via TFT_eSPI getTouch().
 *
 * Calibration model:
 *   mapped_x = (raw_x - cal_x_min) * 320 / (cal_x_max - cal_x_min)
 *   mapped_y = (raw_y - cal_y_min) * 480 / (cal_y_max - cal_y_min)
 *   Clamped to [0, 319] / [0, 479].
 *
 * NVS namespace: "touch"
 * Keys: cal_x_min, cal_x_max, cal_y_min, cal_y_max (i32), calibrated (u8)
 *
 * Goal 1 — no-cal-required default:
 *   Boot goes directly to the launcher using Phase 0 hardcoded values.
 *   Calibration is only run on explicit user request (Settings → Recalibrate).
 *   NVS values are validated; corrupt values fall back to hardcoded defaults.
 *
 * Goal 3 — single TFT_eSPI instance:
 *   Uses display::get_tft() instead of a local s_tft to eliminate the
 *   dual-SPI-instance problem flagged in the Phase 1 architecture review.
 */

/* ── Phase 0 hardcoded defaults (Setup77 values, proven to work) ─── */
static const int32_t kDefaultXMin = 275;
static const int32_t kDefaultXMax = 3620;
static const int32_t kDefaultYMin = 264;
static const int32_t kDefaultYMax = 3532;

/* ── Calibration state ───────────────────────────────────────────── */
static int32_t s_x_min, s_x_max, s_y_min, s_y_max;

/* ── Goal 1: debug logging flag ──────────────────────────────────── */
static bool s_debug_touch = false;

/**
 * Validate a set of calibration values.
 * Returns false if any value is out of range or the set is clearly degenerate.
 */
static bool cal_values_valid(int32_t xmin, int32_t xmax, int32_t ymin, int32_t ymax) {
    if (xmin < 0 || xmax > 4095 || ymin < 0 || ymax > 4095) return false;
    if (xmin >= xmax || ymin >= ymax) return false;
    /* All four identical → almost certainly unwritten garbage */
    if (xmin == xmax && xmax == ymin && ymin == ymax) return false;
    return true;
}

/* ── LVGL indev read callback ────────────────────────────────────── */
static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    TFT_eSPI* tft = display::get_tft();
    uint16_t raw_x = 0, raw_y = 0;
    bool touched = tft->getTouch(&raw_x, &raw_y);

    /* Statics for debug throttle / edge detection */
    static uint32_t s_last_dbg_ms = 0;
    static bool     s_dbg_pressed  = false;

    if (touched) {
        /* Affine map raw ADC → screen pixels */
        int32_t mx = ((int32_t)raw_x - s_x_min) * 320 / (s_x_max - s_x_min);
        int32_t my = ((int32_t)raw_y - s_y_min) * 480 / (s_y_max - s_y_min);

        /* Clamp */
        if (mx < 0)   mx = 0;
        if (mx > 319) mx = 319;
        if (my < 0)   my = 0;
        if (my > 479) my = 479;

        data->point.x = (lv_coord_t)mx;
        data->point.y = (lv_coord_t)my;
        data->state   = LV_INDEV_STATE_PRESSED;

        /* Goal 1: debug logging, throttled to ≤10 Hz (100 ms gate) */
        if (s_debug_touch) {
            uint32_t now = millis();
            if (now - s_last_dbg_ms >= 100) {
                s_last_dbg_ms = now;
                Serial.printf("[touch D] raw=(%u,%u) mapped=(%ld,%ld) pressed=Y\n",
                              raw_x, raw_y, (long)mx, (long)my);
            }
            s_dbg_pressed = true;
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;

        /* Log one release edge when debug is active */
        if (s_debug_touch && s_dbg_pressed) {
            Serial.println("[touch D] raw=(--,--) mapped=(--,--) pressed=N");
            s_dbg_pressed = false;
        }
    }
}

/* ── 4-corner calibration ────────────────────────────────────────── */

/**
 * Collect N raw samples for a given corner.
 * Discards min + max, averages the remaining 3.
 */
static void collect_corner(const char* prompt_text, int n, int32_t* out_x, int32_t* out_y) {
    TFT_eSPI* tft = display::get_tft();

    lv_obj_t* overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_center(overlay);

    lv_obj_t* lbl = lv_label_create(overlay);
    lv_label_set_text(lbl, prompt_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(lbl);

    lv_refr_now(NULL);

    uint16_t rx[5], ry[5];
    int collected = 0;

    while (collected < n) {
        uint16_t tx, ty;
        if (tft->getTouch(&tx, &ty)) {
            if (collected < 5) {
                rx[collected] = tx;
                ry[collected] = ty;
                collected++;
            }
            delay(50);
        }
        delay(10);
        lv_timer_handler();
    }

    uint16_t rx_tmp, ry_tmp;
    while (tft->getTouch(&rx_tmp, &ry_tmp)) { delay(20); }
    delay(200);

    int32_t sx = 0, sy = 0;
    int32_t mn_x = rx[0], mx_x = rx[0];
    int32_t mn_y = ry[0], mx_y = ry[0];
    for (int i = 1; i < n; i++) {
        if (rx[i] < mn_x) mn_x = rx[i];
        if (rx[i] > mx_x) mx_x = rx[i];
        if (ry[i] < mn_y) mn_y = ry[i];
        if (ry[i] > mx_y) mx_y = ry[i];
    }
    int used = 0;
    for (int i = 0; i < n; i++) {
        if (rx[i] != mn_x && rx[i] != mx_x) { sx += rx[i]; sy += ry[i]; used++; }
    }
    if (used > 0) {
        *out_x = sx / used;
        *out_y = sy / used;
    } else {
        sx = sy = 0;
        for (int i = 0; i < n; i++) { sx += rx[i]; sy += ry[i]; }
        *out_x = sx / n;
        *out_y = sy / n;
    }

    lv_obj_del(overlay);
}

void touch::run_calibration() {
    TFT_eSPI* tft = display::get_tft();
    LOG_I("touch", "Starting 4-corner calibration");

    int32_t tl_x, tl_y;
    int32_t tr_x, tr_y;
    int32_t bl_x, bl_y;
    int32_t br_x, br_y;

    collect_corner("Tap TOP-LEFT corner",     5, &tl_x, &tl_y);
    collect_corner("Tap TOP-RIGHT corner",    5, &tr_x, &tr_y);
    collect_corner("Tap BOTTOM-LEFT corner",  5, &bl_x, &bl_y);
    collect_corner("Tap BOTTOM-RIGHT corner", 5, &br_x, &br_y);

    s_x_min = (tl_x + bl_x) / 2;
    s_x_max = (tr_x + br_x) / 2;
    s_y_min = (tl_y + tr_y) / 2;
    s_y_max = (bl_y + br_y) / 2;

    nvs_store::put_i32("touch", "cal_x_min", s_x_min);
    nvs_store::put_i32("touch", "cal_x_max", s_x_max);
    nvs_store::put_i32("touch", "cal_y_min", s_y_min);
    nvs_store::put_i32("touch", "cal_y_max", s_y_max);
    nvs_store::put_u8("touch", "calibrated", 1);

    LOG_I("touch", "Cal done: x[%ld..%ld] y[%ld..%ld]",
          (long)s_x_min, (long)s_x_max, (long)s_y_min, (long)s_y_max);

    lv_obj_t* msg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(msg, 280, 80);
    lv_obj_center(msg);
    lv_obj_t* lbl = lv_label_create(msg);
    lv_label_set_text(lbl, "Calibration saved!\nTouch to continue");
    lv_obj_center(lbl);
    lv_refr_now(NULL);

    uint16_t dx, dy;
    while (!tft->getTouch(&dx, &dy)) { delay(20); }
    while (tft->getTouch(&dx, &dy))  { delay(20); }

    lv_obj_del(msg);
}

namespace touch {

/* ── Goal 1: debug toggle ────────────────────────────────────────── */

void set_debug(bool enabled) {
    s_debug_touch = enabled;
    Serial.printf("[touch] Debug logging %s\n", enabled ? "ENABLED" : "DISABLED");
}

bool get_debug() {
    return s_debug_touch;
}

/* ── Hot-reload calibration values ──────────────────────────────── */

void apply_cal(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max) {
    s_x_min = x_min;
    s_x_max = x_max;
    s_y_min = y_min;
    s_y_max = y_max;
    LOG_I("touch", "Cal applied (hot): x[%ld..%ld] y[%ld..%ld]",
          (long)s_x_min, (long)s_x_max, (long)s_y_min, (long)s_y_max);
}

void init() {
    /* Goal 1: Default to Phase 0 hardcoded cal values; do NOT auto-run
     * calibration on first boot.  If NVS has valid cal values, use them.
     * If absent or corrupt, fall back to hardcoded defaults and log a warning.
     * Calibration is only triggered explicitly via touch::run_calibration()
     * (hooked up to Settings → Recalibrate Touch).
     *
     * Goal 3: Use display::get_tft() in all touch reads — no local TFT_eSPI.
     */

    uint8_t cal = nvs_store::get_u8("touch", "calibrated", 0);
    if (cal) {
        int32_t xmin = nvs_store::get_i32("touch", "cal_x_min", kDefaultXMin);
        int32_t xmax = nvs_store::get_i32("touch", "cal_x_max", kDefaultXMax);
        int32_t ymin = nvs_store::get_i32("touch", "cal_y_min", kDefaultYMin);
        int32_t ymax = nvs_store::get_i32("touch", "cal_y_max", kDefaultYMax);

        if (cal_values_valid(xmin, xmax, ymin, ymax)) {
            s_x_min = xmin; s_x_max = xmax;
            s_y_min = ymin; s_y_max = ymax;
            LOG_I("touch", "Loaded NVS cal x[%ld..%ld] y[%ld..%ld]",
                  (long)s_x_min, (long)s_x_max, (long)s_y_min, (long)s_y_max);
        } else {
            LOG_W("touch", "NVS cal values corrupt — falling back to Phase 0 defaults");
            s_x_min = kDefaultXMin; s_x_max = kDefaultXMax;
            s_y_min = kDefaultYMin; s_y_max = kDefaultYMax;
        }
    } else {
        /* No cal in NVS — use Phase 0 hardcoded defaults, skip cal prompt */
        s_x_min = kDefaultXMin; s_x_max = kDefaultXMax;
        s_y_min = kDefaultYMin; s_y_max = kDefaultYMax;
        LOG_I("touch", "No NVS cal — using Phase 0 defaults x[%ld..%ld] y[%ld..%ld]",
              (long)s_x_min, (long)s_x_max, (long)s_y_min, (long)s_y_max);
        /* Goal 4: prompt user to calibrate without blocking boot */
        Serial.println("[touch] No cal in NVS. Type 'cal' to calibrate, or any tap will use Phase 0 defaults.");
    }

    /* Register LVGL input device */
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
}

} // namespace touch

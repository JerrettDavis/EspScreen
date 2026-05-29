#include "touch.h"
#include "display.h"
#include "../os/nvs_store.h"
#include "../os/logger.h"
#include <TFT_eSPI.h>
#include <lvgl.h>

/**
 * Touch driver — XPT2046 via TFT_eSPI getTouch().
 *
 * Calibration model (signed-delta, handles axis swap + direction inversion):
 *   If axis_swap==false:
 *     mapped_x = (raw_x - x_at_left) * 320 / (x_at_right - x_at_left)
 *     mapped_y = (raw_y - y_at_top)  * 480 / (y_at_bottom - y_at_top)
 *   If axis_swap==true (panel mounted 90° — raw_y runs horizontally):
 *     mapped_x = (raw_y - x_at_left) * 320 / (x_at_right - x_at_left)
 *     mapped_y = (raw_x - y_at_top)  * 480 / (y_at_bottom - y_at_top)
 *   Both clamped to [0,319] / [0,479].
 *   Signed delta means x_at_right can be < x_at_left (mirrored panel) and the
 *   math still produces the correct mapping automatically.
 *
 * NVS namespace: "touch"
 * Keys: cal_x_min (=x_at_left), cal_x_max (=x_at_right),
 *       cal_y_min (=y_at_top),  cal_y_max (=y_at_bottom)  — all i32
 *       calibrated (u8), axis_swap (u8, new)
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
/* x_at_left / x_at_right: raw ADC for the screen-X axis at left / right edge.
 * y_at_top  / y_at_bottom: raw ADC for the screen-Y axis at top / bottom edge.
 * axis_swap: when true, raw_y feeds screen-X and raw_x feeds screen-Y.
 * Signed deltas mean inverted (mirrored) panels work without extra flags. */
static int32_t s_x_at_left, s_x_at_right, s_y_at_top, s_y_at_bottom;
static bool    s_axis_swap = false;

/* ── Goal 1: debug logging flag ──────────────────────────────────── */
static bool s_debug_touch = false;

/* ── Pressure / IRQ gating ───────────────────────────────────────── */
constexpr int TOUCH_IRQ_PIN = 36;   /* XPT2046 PENIRQ — active LOW when touched */

/**
 * Validate a set of calibration values (signed-delta model).
 * x_at_left / x_at_right and y_at_top / y_at_bottom can be in either order
 * (the panel may be inverted), but the absolute spread must be >= 500 counts.
 */
static bool cal_values_valid(int32_t x_at_left, int32_t x_at_right,
                             int32_t y_at_top,  int32_t y_at_bottom) {
    /* Values must be plausible ADC readings */
    auto inRange = [](int32_t v) { return v >= 0 && v <= 4095; };
    if (!inRange(x_at_left) || !inRange(x_at_right) ||
        !inRange(y_at_top)  || !inRange(y_at_bottom)) return false;
    /* Spread must be at least 500 counts (otherwise it's degenerate / garbage) */
    if (abs(x_at_right - x_at_left) < 500) return false;
    if (abs(y_at_bottom - y_at_top) < 500) return false;
    return true;
}

/* ── Pressure + IRQ gate ─────────────────────────────────────────── */
/**
 * Returns true only when the XPT2046 is confirming a real physical touch:
 *   1. GPIO36 (PENIRQ) must be LOW — the chip drives it LOW when touched.
 *      (Input-only pin, no internal pull-up; external pull-up on XPT2046 board.)
 *   2. Z-pressure channel must read below 600 ADC counts.  When no stylus is
 *      present the Z channel floats high (~3000+).  Real touches are typically
 *      100–500.  600 is a conservative threshold that leaves headroom for
 *      stylus-tip variation while cleanly rejecting phantom reads.
 */
namespace touch {
    bool is_real_touch() {
        /* Fast path: IRQ pin HIGH means no touch — skip SPI entirely */
        if (digitalRead(TOUCH_IRQ_PIN) == HIGH) return false;
        /* Confirm with Z channel (requires SPI) */
        TFT_eSPI* tft = display::get_tft();
        uint16_t z = tft->getTouchRawZ();
        return z > 0 && z < 600;
    }
}

/* ── LVGL indev read callback ────────────────────────────────────── */
static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    /* Statics for debug throttle / edge detection */
    static uint32_t s_last_dbg_ms = 0;
    static bool     s_dbg_pressed  = false;

    /* Pressure + IRQ gate — reject phantom reads before any SPI-heavy getTouch */
    if (!touch::is_real_touch()) {
        data->state = LV_INDEV_STATE_RELEASED;
        if (s_debug_touch && s_dbg_pressed) {
            uint16_t z = display::get_tft()->getTouchRawZ();
            int irq = digitalRead(TOUCH_IRQ_PIN);
            Serial.printf("[touch D] raw=(--,--) z=%u irq=%s real_touch=N pressed=N\n",
                          z, irq == LOW ? "L" : "H");
            s_dbg_pressed = false;
        }
        return;
    }

    TFT_eSPI* tft = display::get_tft();
    uint16_t raw_x = 0, raw_y = 0;
    bool touched = tft->getTouchRaw(&raw_x, &raw_y);

    if (touched) {
        /* Apply axis swap: when panel is mounted 90°, raw_y feeds screen-X. */
        int32_t val_x = s_axis_swap ? (int32_t)raw_y : (int32_t)raw_x;
        int32_t val_y = s_axis_swap ? (int32_t)raw_x : (int32_t)raw_y;

        /* Signed-delta affine map — handles inversion (x_at_right < x_at_left) */
        int32_t dx = s_x_at_right - s_x_at_left;
        int32_t dy = s_y_at_bottom - s_y_at_top;
        int32_t mx = (dx != 0) ? ((val_x - s_x_at_left) * 320 / dx) : 0;
        int32_t my = (dy != 0) ? ((val_y - s_y_at_top)  * 480 / dy) : 0;

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
                uint16_t z = tft->getTouchRawZ();
                int irq = digitalRead(TOUCH_IRQ_PIN);
                Serial.printf("[touch D] raw=(%u,%u) z=%u irq=%s swap=%s val=(%ld,%ld) mapped=(%ld,%ld) real_touch=Y\n",
                              raw_x, raw_y, z, irq == LOW ? "L" : "H",
                              s_axis_swap ? "Y" : "N",
                              (long)val_x, (long)val_y, (long)mx, (long)my);
            }
            s_dbg_pressed = true;
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;

        /* Log one release edge when debug is active */
        if (s_debug_touch && s_dbg_pressed) {
            Serial.println("[touch D] raw=(--,--) z=-- irq=-- real_touch=N pressed=N");
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

    s_x_at_left   = (tl_x + bl_x) / 2;
    s_x_at_right  = (tr_x + br_x) / 2;
    s_y_at_top    = (tl_y + tr_y) / 2;
    s_y_at_bottom = (bl_y + br_y) / 2;
    s_axis_swap   = false;  /* legacy blocking cal assumes normal orientation */

    nvs_store::put_i32("touch", "cal_x_min",  s_x_at_left);
    nvs_store::put_i32("touch", "cal_x_max",  s_x_at_right);
    nvs_store::put_i32("touch", "cal_y_min",  s_y_at_top);
    nvs_store::put_i32("touch", "cal_y_max",  s_y_at_bottom);
    nvs_store::put_u8 ("touch", "axis_swap",  0);
    nvs_store::put_u8 ("touch", "calibrated", 1);

    LOG_I("touch", "Cal done: x[%ld->%ld] y[%ld->%ld]",
          (long)s_x_at_left, (long)s_x_at_right,
          (long)s_y_at_top,  (long)s_y_at_bottom);

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

void apply_cal(int32_t x_at_left, int32_t x_at_right,
               int32_t y_at_top,  int32_t y_at_bottom,
               bool axis_swap) {
    s_x_at_left   = x_at_left;
    s_x_at_right  = x_at_right;
    s_y_at_top    = y_at_top;
    s_y_at_bottom = y_at_bottom;
    s_axis_swap   = axis_swap;
    LOG_I("touch", "Cal applied (hot): swap=%s x[%ld->%ld] y[%ld->%ld]",
          axis_swap ? "Y" : "N",
          (long)s_x_at_left, (long)s_x_at_right,
          (long)s_y_at_top,  (long)s_y_at_bottom);
}

void init() {
    /* Goal 1: Default to Phase 0 hardcoded cal values; do NOT auto-run
     * calibration on first boot.  If NVS has valid cal values, use them.
     * If absent or corrupt, fall back to hardcoded defaults and log a warning.
     * Calibration is only triggered explicitly via touch::run_calibration()
     * (hooked up to Settings → Recalibrate Touch).
     *
     * Goal 3: Use display::get_tft() in all touch reads — no local TFT_eSPI.
     *
     * NVS key axis_swap (u8, default 0): set by calibrate.cpp when it detects
     * that the panel's physical X wire runs along the screen's vertical axis.
     */

    uint8_t cal = nvs_store::get_u8("touch", "calibrated", 0);
    if (cal) {
        int32_t xl = nvs_store::get_i32("touch", "cal_x_min", kDefaultXMin);
        int32_t xr = nvs_store::get_i32("touch", "cal_x_max", kDefaultXMax);
        int32_t yt = nvs_store::get_i32("touch", "cal_y_min", kDefaultYMin);
        int32_t yb = nvs_store::get_i32("touch", "cal_y_max", kDefaultYMax);
        bool    sw = nvs_store::get_u8("touch", "axis_swap", 0) != 0;

        if (cal_values_valid(xl, xr, yt, yb)) {
            s_x_at_left   = xl; s_x_at_right  = xr;
            s_y_at_top    = yt; s_y_at_bottom = yb;
            s_axis_swap   = sw;
            LOG_I("touch", "Loaded NVS cal swap=%s x[%ld->%ld] y[%ld->%ld]",
                  sw ? "Y" : "N",
                  (long)s_x_at_left, (long)s_x_at_right,
                  (long)s_y_at_top,  (long)s_y_at_bottom);
        } else {
            LOG_W("touch", "NVS cal values corrupt — falling back to Phase 0 defaults");
            s_x_at_left  = kDefaultXMin; s_x_at_right  = kDefaultXMax;
            s_y_at_top   = kDefaultYMin; s_y_at_bottom = kDefaultYMax;
            s_axis_swap  = false;
        }
    } else {
        /* No cal in NVS — use Phase 0 hardcoded defaults, skip cal prompt */
        s_x_at_left  = kDefaultXMin; s_x_at_right  = kDefaultXMax;
        s_y_at_top   = kDefaultYMin; s_y_at_bottom = kDefaultYMax;
        s_axis_swap  = false;
        LOG_I("touch", "No NVS cal — using Phase 0 defaults x[%ld->%ld] y[%ld->%ld]",
              (long)s_x_at_left, (long)s_x_at_right,
              (long)s_y_at_top,  (long)s_y_at_bottom);
        /* Goal 4: prompt user to calibrate without blocking boot */
        Serial.println("[touch] No cal in NVS. Type 'cal' to calibrate, or any tap will use Phase 0 defaults.");
    }

    /* GPIO36 is input-only on ESP32 — no internal pull-up available.
     * The XPT2046 board has an external pull-up on PENIRQ.  Just configure
     * as plain INPUT; setting INPUT_PULLUP would be silently ignored anyway. */
    pinMode(TOUCH_IRQ_PIN, INPUT);
    Serial.printf("[touch] IRQ pin GPIO%d configured as INPUT (active LOW when touched)\n",
                  TOUCH_IRQ_PIN);

    /* Startup diagnostic — visible in serial monitor at every boot */
    Serial.printf("[touch] init complete: cal x_at_left=%ld x_at_right=%ld y_at_top=%ld y_at_bottom=%ld axis_swap=%d validated=%s\n",
                  (long)s_x_at_left, (long)s_x_at_right,
                  (long)s_y_at_top,  (long)s_y_at_bottom,
                  (int)s_axis_swap,
                  cal_values_valid(s_x_at_left, s_x_at_right, s_y_at_top, s_y_at_bottom) ? "YES" : "NO");

    /* Register LVGL input device */
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
}

} // namespace touch

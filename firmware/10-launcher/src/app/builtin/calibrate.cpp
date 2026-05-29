/**
 * calibrate.cpp — Hold-to-settle touch calibration screen.
 *
 * UX flow per corner:
 *   1. Display crosshair + progress arc at the target point.
 *   2. User presses and HOLDS the stylus on the target.
 *   3. Firmware samples raw XPT2046 readings at ~30 Hz.
 *   4. First 30 samples build the sliding window (arc 0%→50%).
 *   5. Once the window has ≥30 samples AND std-dev for both X and Y
 *      is < 15 ADC counts, a stability counter increments.
 *      30 consecutive stable samples fills arc 50%→100%.
 *      If std-dev spikes above 15, stability counter resets to 0
 *      and the arc decays back to 50%.
 *   6. On 100%: record mean, flash crosshair green, wait 300 ms, next corner.
 *   7. Release before 100%: reset state back to "Hold steady...".
 *   8. No touch for 30 s: abort, pop back without saving.
 *
 * Raw reads use display::get_tft()->getTouch() directly (bypasses the
 * currently-mis-calibrated affine map in touch_read_cb).
 *
 * After all 4 corners:
 *   - Compute affine bounds from corner means.
 *   - Sanity checks (min < max, values 100..4000).
 *   - Save to NVS, hot-apply via touch::apply_cal().
 *   - Show "Calibration saved!" and wait for any tap, then pop().
 */

#include "calibrate.h"
#include "../../hal/touch.h"
#include "../../hal/display.h"
#include "../../os/nvs_store.h"
#include "../../os/screen_router.h"
#include "../../os/logger.h"
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Arduino.h>
#include <math.h>

/* ── Screen geometry ─────────────────────────────────────────────── */
#define SCREEN_W   320
#define SCREEN_H   480

/* ── Corner target coordinates (screen pixels, 0-based) ─────────── */
static const lv_point_t kCorners[4] = {
    {  20,  20 },   /* 0: Top-Left     */
    { 300,  20 },   /* 1: Top-Right    */
    { 300, 460 },   /* 2: Bottom-Right */
    {  20, 460 },   /* 3: Bottom-Left  */
};
static const char* kCornerNames[4] = {
    "Top-Left", "Top-Right", "Bottom-Right", "Bottom-Left"
};

/* ── Sampling parameters ─────────────────────────────────────────── */
#define SAMPLE_HZ          30        /* samples per second            */
#define SAMPLE_INTERVAL_MS 33        /* 1000/30 ≈ 33 ms               */
#define WINDOW_SIZE        20        /* sliding window length (was 30) */
#define STABLE_CYCLES      15        /* consecutive stable cycles req. (was 30) */
#define STDDEV_THRESHOLD   30.0f     /* ADC counts (was 15.0 — XPT2046 is noisy) */
#define TIMEOUT_MS         30000     /* abandon if no touch for 30 s   */

/* ── LVGL widget handles ─────────────────────────────────────────── */
static lv_obj_t* s_scr          = nullptr;
static lv_obj_t* s_title_lbl    = nullptr;
static lv_obj_t* s_sub_lbl      = nullptr;
static lv_obj_t* s_arc          = nullptr;
static lv_obj_t* s_status_lbl   = nullptr;
static lv_obj_t* s_h_line       = nullptr;   /* horizontal bar of crosshair */
static lv_obj_t* s_v_line       = nullptr;   /* vertical bar of crosshair   */
static lv_obj_t* s_cancel_btn   = nullptr;

/* ── State machine ───────────────────────────────────────────────── */
static int     s_corner          = 0;
static bool    s_cal_running     = false;
static lv_timer_t* s_timer       = nullptr;

/* Corner raw mean results */
static int32_t s_corner_x[4];
static int32_t s_corner_y[4];

/* Per-corner sample state */
static uint16_t s_win_x[WINDOW_SIZE];
static uint16_t s_win_y[WINDOW_SIZE];
static int      s_win_count      = 0;   /* samples in window (capped at WINDOW_SIZE) */
static int      s_win_head       = 0;   /* ring-buffer write index                   */
static int      s_stable_count   = 0;   /* consecutive stable cycles                 */
static bool     s_was_touched    = false;
static uint32_t s_last_sample_ms = 0;
static uint32_t s_last_touch_ms  = 0;   /* for timeout                               */

/* ── Maths helpers ───────────────────────────────────────────────── */

static float window_mean(const uint16_t* buf, int n) {
    if (n <= 0) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += buf[i];
    return sum / (float)n;
}

static float window_stddev(const uint16_t* buf, int n, float mean) {
    if (n <= 1) return 999.0f;   /* not enough data */
    float var = 0.0f;
    for (int i = 0; i < n; i++) {
        float d = (float)buf[i] - mean;
        var += d * d;
    }
    return sqrtf(var / (float)(n - 1));
}

/* ── Crosshair drawing helpers ───────────────────────────────────── */

/*
 * lv_line needs its point arrays to persist; use statics per line.
 */
static lv_point_precise_t s_h_pts[2];
static lv_point_precise_t s_v_pts[2];

static void position_crosshair(int cx, int cy, lv_color_t color) {
    const int ARM = 20;   /* half-length of each crosshair arm */

    s_h_pts[0] = { (lv_value_precise_t)(cx - ARM), (lv_value_precise_t)cy };
    s_h_pts[1] = { (lv_value_precise_t)(cx + ARM), (lv_value_precise_t)cy };
    s_v_pts[0] = { (lv_value_precise_t)cx, (lv_value_precise_t)(cy - ARM) };
    s_v_pts[1] = { (lv_value_precise_t)cx, (lv_value_precise_t)(cy + ARM) };

    lv_line_set_points(s_h_line, s_h_pts, 2);
    lv_line_set_points(s_v_line, s_v_pts, 2);

    lv_obj_set_style_line_color(s_h_line, color, 0);
    lv_obj_set_style_line_color(s_v_line, color, 0);

    /* Position arc centred on the same point */
    lv_obj_set_pos(s_arc,
                   cx - lv_obj_get_width(s_arc)  / 2,
                   cy - lv_obj_get_height(s_arc) / 2);
}

/* ── UI update helper ────────────────────────────────────────────── */

static void set_status(const char* txt) {
    if (s_status_lbl) lv_label_set_text(s_status_lbl, txt);
}

static void set_arc(int32_t pct) {
    if (s_arc) lv_arc_set_value(s_arc, (int32_t)pct);
}

static void set_title(int corner_idx) {
    char buf[48];
    snprintf(buf, sizeof(buf), "Touch Calibration — %d of 4", corner_idx + 1);
    if (s_title_lbl) lv_label_set_text(s_title_lbl, buf);
}

/* ── Advance to next corner (or finish) ──────────────────────────── */

static void advance_corner();
static void finish_calibration();
static void abort_calibration(const char* reason);

/* ── LVGL timer callback — called every SAMPLE_INTERVAL_MS ──────── */

static void cal_timer_cb(lv_timer_t* timer) {
    if (!s_cal_running) return;

    TFT_eSPI* tft = display::get_tft();
    uint16_t rx = 0, ry = 0;
    bool touched = tft->getTouch(&rx, &ry);
    uint32_t now = millis();

    /* Timeout: no touch for TIMEOUT_MS */
    if (touched) {
        s_last_touch_ms = now;
    } else if (now - s_last_touch_ms > TIMEOUT_MS) {
        Serial.printf("[cal] Corner %d TIMEOUT after 30s — aborting\n", s_corner);
        abort_calibration("Timeout — no touch detected");
        return;
    }

    if (touched) {
        /* Log first touch on this corner */
        if (!s_was_touched && s_win_count == 0) {
            Serial.printf("[cal] Corner %d (%s): start sampling\n",
                          s_corner, kCornerNames[s_corner]);
        }

        /* Rate-limit sampling */
        if (now - s_last_sample_ms < SAMPLE_INTERVAL_MS) return;
        s_last_sample_ms = now;

        /* Store into ring buffer */
        s_win_x[s_win_head] = rx;
        s_win_y[s_win_head] = ry;
        s_win_head = (s_win_head + 1) % WINDOW_SIZE;
        if (s_win_count < WINDOW_SIZE) s_win_count++;

        s_was_touched = true;

        /* Compute stats on full window */
        float mx = window_mean(s_win_x, s_win_count);
        float my = window_mean(s_win_y, s_win_count);
        float sx = window_stddev(s_win_x, s_win_count, mx);
        float sy = window_stddev(s_win_y, s_win_count, my);

        if (s_win_count < WINDOW_SIZE) {
            /* Phase 1: filling the window — arc 0%..50% */
            int32_t pct = (int32_t)((float)s_win_count / (float)WINDOW_SIZE * 50.0f);
            set_arc(pct);
            char buf[40];
            snprintf(buf, sizeof(buf), "Sampling: %d/%d", s_win_count, WINDOW_SIZE);
            set_status(buf);
        } else {
            /* Log window-full event once (when count first hits WINDOW_SIZE) */
            static int s_last_logged_full_corner = -1;
            if (s_last_logged_full_corner != s_corner) {
                s_last_logged_full_corner = s_corner;
                Serial.printf("[cal] Corner %d: window full, mean=(%.0f,%.0f) stddev=(%.1f,%.1f)\n",
                              s_corner, mx, my, sx, sy);
            }

            /* Phase 2: window full, check stability */
            bool stable = (sx < STDDEV_THRESHOLD && sy < STDDEV_THRESHOLD);
            if (stable) {
                s_stable_count++;
                /* Log every 5 stable cycles, not every cycle */
                if (s_stable_count % 5 == 0) {
                    Serial.printf("[cal] Corner %d: stable cycle %d/%d (stddev=(%.1f,%.1f))\n",
                                  s_corner, s_stable_count, STABLE_CYCLES, sx, sy);
                }
            } else {
                /* Spike — decay arc back to 50% and reset stability counter */
                if (s_stable_count > 0) {
                    /* Only log resets, not every unstable sample from scratch */
                    Serial.printf("[cal] Corner %d: stability lost (stddev=(%.1f,%.1f)) — counter reset from %d\n",
                                  s_corner, sx, sy, s_stable_count);
                }
                s_stable_count = 0;
                set_arc(50);
                char buf[64];
                snprintf(buf, sizeof(buf), "Hold steady... (jitter %.0f,%.0f)", sx, sy);
                set_status(buf);
                return;
            }

            int32_t pct = 50 + (int32_t)((float)s_stable_count / (float)STABLE_CYCLES * 50.0f);
            if (pct > 100) pct = 100;
            set_arc(pct);

            if (s_stable_count >= STABLE_CYCLES) {
                /* 100%: record this corner */
                s_corner_x[s_corner] = (int32_t)mx;
                s_corner_y[s_corner] = (int32_t)my;

                Serial.printf("[cal] Corner %d CONFIRMED: raw=(%.0f,%.0f) stddev=(%.1f,%.1f)\n",
                              s_corner, mx, my, sx, sy);
                LOG_I("cal", "Corner %d (%s): raw mean (%ld, %ld) stddev (%.1f, %.1f)",
                      s_corner, kCornerNames[s_corner],
                      (long)s_corner_x[s_corner], (long)s_corner_y[s_corner], sx, sy);

                /* Green flash */
                position_crosshair(kCorners[s_corner].x, kCorners[s_corner].y,
                                   lv_color_hex(0x00ff00));
                set_status("Stable! Lift to confirm");
                lv_refr_now(NULL);
                delay(300);

                /* Wait for lift */
                uint16_t dx, dy;
                while (tft->getTouch(&dx, &dy)) { delay(10); }

                advance_corner();
            } else {
                char buf[48];
                snprintf(buf, sizeof(buf), "Stable: %d/%d", s_stable_count, STABLE_CYCLES);
                set_status(buf);
            }
        }
    } else {
        /* Touch released before 100% — reset */
        if (s_was_touched) {
            s_was_touched   = false;
            s_win_count     = 0;
            s_win_head      = 0;
            s_stable_count  = 0;
            set_arc(0);
            set_status("Hold steady...");
        }
    }
}

/* ── Advance to next corner ──────────────────────────────────────── */

static void advance_corner() {
    s_corner++;
    if (s_corner >= 4) {
        finish_calibration();
        return;
    }

    /* Reset state for next corner */
    s_win_count    = 0;
    s_win_head     = 0;
    s_stable_count = 0;
    s_was_touched  = false;
    s_last_touch_ms = millis();
    set_arc(0);
    set_status("Hold steady...");
    set_title(s_corner);

    position_crosshair(kCorners[s_corner].x, kCorners[s_corner].y,
                       lv_color_hex(0xffffff));
    lv_refr_now(NULL);
}

/* ── Finish: validate, save, hot-apply ───────────────────────────── */

static void finish_calibration() {
    s_cal_running = false;
    if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }

    /* Dump all raw corner values */
    Serial.printf("[cal] TL=(%ld,%ld) TR=(%ld,%ld) BR=(%ld,%ld) BL=(%ld,%ld)\n",
                  (long)s_corner_x[0], (long)s_corner_y[0],
                  (long)s_corner_x[1], (long)s_corner_y[1],
                  (long)s_corner_x[2], (long)s_corner_y[2],
                  (long)s_corner_x[3], (long)s_corner_y[3]);

    /* Orientation sanity: TL.x should be < TR.x, TL.y should be < BL.y */
    Serial.printf("[cal] orient check: TL.x=%ld TR.x=%ld (expect TL<TR); TL.y=%ld BL.y=%ld (expect TL<BL)\n",
                  (long)s_corner_x[0], (long)s_corner_x[1],
                  (long)s_corner_y[0], (long)s_corner_y[3]);
    if (s_corner_x[0] > s_corner_x[1]) {
        Serial.println("[cal] WARN: X axis appears FLIPPED — TL.x > TR.x");
    }
    if (s_corner_y[0] > s_corner_y[3]) {
        Serial.println("[cal] WARN: Y axis appears FLIPPED — TL.y > BL.y");
    }

    /* Compute affine bounds from 4 corners */
    int32_t x_min = (s_corner_x[0] + s_corner_x[3]) / 2;   /* TL.x + BL.x */
    int32_t x_max = (s_corner_x[1] + s_corner_x[2]) / 2;   /* TR.x + BR.x */
    int32_t y_min = (s_corner_y[0] + s_corner_y[1]) / 2;   /* TL.y + TR.y */
    int32_t y_max = (s_corner_y[3] + s_corner_y[2]) / 2;   /* BL.y + BR.y */

    Serial.printf("[cal] cal_x_min=%ld cal_x_max=%ld cal_y_min=%ld cal_y_max=%ld\n",
                  (long)x_min, (long)x_max, (long)y_min, (long)y_max);
    LOG_I("cal", "Computed cal: x[%ld..%ld] y[%ld..%ld]",
          (long)x_min, (long)x_max, (long)y_min, (long)y_max);

    /* Sanity check 1: min < max */
    if (x_min >= x_max || y_min >= y_max) {
        Serial.printf("[cal] REJECT: x_min(%ld) >= x_max(%ld) or y_min(%ld) >= y_max(%ld)\n",
                      (long)x_min, (long)x_max, (long)y_min, (long)y_max);
        abort_calibration("Error: degenerate cal (min >= max)");
        return;
    }

    /* Sanity check 2: range must be at least 500 ADC counts per axis */
    int32_t x_range = x_max - x_min;
    int32_t y_range = y_max - y_min;
    if (x_range < 500 || y_range < 500) {
        Serial.printf("[cal] REJECT: range too small — x_range=%ld y_range=%ld (need >=500 each)\n",
                      (long)x_range, (long)y_range);
        abort_calibration("Error: cal range too small (<500 ADC counts)");
        return;
    }

    /* Sanity check 3: stuck ADC (exactly 0 or 4095) */
    if (x_min == 0 || x_max == 4095 || y_min == 0 || y_max == 4095) {
        Serial.printf("[cal] REJECT: stuck ADC value (0 or 4095) in x[%ld..%ld] y[%ld..%ld]\n",
                      (long)x_min, (long)x_max, (long)y_min, (long)y_max);
        abort_calibration("Error: stuck ADC (0 or 4095 detected)");
        return;
    }

    /* NOTE: Removed the old 100..4000 outer-bound check — some panels read outside that range */
    Serial.println("[cal] SAVE: ok — all sanity checks passed");

    /* Save to NVS */
    nvs_store::put_i32("touch", "cal_x_min", x_min);
    nvs_store::put_i32("touch", "cal_x_max", x_max);
    nvs_store::put_i32("touch", "cal_y_min", y_min);
    nvs_store::put_i32("touch", "cal_y_max", y_max);
    nvs_store::put_u8 ("touch", "calibrated", 1);
    Serial.println("[cal] NVS write ok");

    /* Hot-apply immediately */
    touch::apply_cal(x_min, x_max, y_min, y_max);

    LOG_I("cal", "Calibration saved and applied");
    Serial.printf("[cal] New cal: x[%ld..%ld] y[%ld..%ld]\n",
                  (long)x_min, (long)x_max, (long)y_min, (long)y_max);

    /* Show success message */
    lv_obj_clean(s_scr);
    lv_obj_t* lbl = lv_label_create(s_scr);
    lv_label_set_text(lbl, "Calibration saved!\nTap anywhere to continue");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x00ff00), 0);
    lv_obj_center(lbl);
    lv_refr_now(NULL);

    /* Wait for any tap using raw getTouch (calibration is now applied) */
    TFT_eSPI* tft = display::get_tft();
    uint16_t dx, dy;
    uint32_t wait_start = millis();
    while (!tft->getTouch(&dx, &dy) && (millis() - wait_start < 15000)) {
        lv_timer_handler();
        delay(20);
    }
    while (tft->getTouch(&dx, &dy)) { delay(20); }

    screen_router::pop();
}

/* ── Abort without saving ────────────────────────────────────────── */

static void abort_calibration(const char* reason) {
    s_cal_running = false;
    if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }

    LOG_W("cal", "Aborted: %s", reason);
    Serial.printf("[cal] Aborted: %s\n", reason);

    if (s_scr) {
        lv_obj_clean(s_scr);
        lv_obj_t* lbl = lv_label_create(s_scr);
        char buf[128];
        snprintf(buf, sizeof(buf), "Calibration cancelled.\n%s\nTap to return.", reason);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xff4444), 0);
        lv_obj_center(lbl);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_refr_now(NULL);

        TFT_eSPI* tft = display::get_tft();
        uint16_t dx, dy;
        uint32_t wait_start = millis();
        while (!tft->getTouch(&dx, &dy) && (millis() - wait_start < 10000)) {
            lv_timer_handler();
            delay(20);
        }
        while (tft->getTouch(&dx, &dy)) { delay(20); }
    }

    screen_router::pop();
}

/* ── Cancel button callback ──────────────────────────────────────── */

static void cancel_cb(lv_event_t* e) {
    abort_calibration("User cancelled");
}

/* ── Build the calibration screen ────────────────────────────────── */

static lv_obj_t* build_screen() {
    /* Full-screen black background */
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* Title label — top centre */
    s_title_lbl = lv_label_create(scr);
    lv_label_set_text(s_title_lbl, "Touch Calibration — 1 of 4");
    lv_obj_set_style_text_color(s_title_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_title_lbl, LV_ALIGN_TOP_MID, 0, 8);

    /* Subtitle */
    s_sub_lbl = lv_label_create(scr);
    lv_label_set_text(s_sub_lbl, "Press and HOLD the crosshair\nuntil the ring completes");
    lv_obj_set_style_text_color(s_sub_lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_align(s_sub_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_sub_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_sub_lbl, SCREEN_W - 20);
    lv_obj_align(s_sub_lbl, LV_ALIGN_TOP_MID, 0, 30);

    /* Crosshair — two lv_line widgets */
    s_h_line = lv_line_create(scr);
    lv_obj_set_style_line_width(s_h_line, 2, 0);
    lv_obj_set_style_line_color(s_h_line, lv_color_hex(0xffffff), 0);

    s_v_line = lv_line_create(scr);
    lv_obj_set_style_line_width(s_v_line, 2, 0);
    lv_obj_set_style_line_color(s_v_line, lv_color_hex(0xffffff), 0);

    /* Arc progress ring (radius 30) */
    s_arc = lv_arc_create(scr);
    lv_arc_set_range(s_arc, 0, 100);
    lv_arc_set_value(s_arc, 0);
    lv_arc_set_rotation(s_arc, 270);   /* start at 12-o'clock */
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_obj_set_size(s_arc, 60, 60);    /* radius ≈ 30 */
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x00aaff), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 4, LV_PART_INDICATOR);
    /* Hide the knob */
    lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);

    /* Status label — centre-ish */
    s_status_lbl = lv_label_create(scr);
    lv_label_set_text(s_status_lbl, "Hold steady...");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_CENTER, 0, 60);

    /* Cancel button — bottom-right */
    s_cancel_btn = lv_button_create(scr);
    lv_obj_set_size(s_cancel_btn, 90, 36);
    lv_obj_align(s_cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_add_event_cb(s_cancel_btn, cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* cancel_lbl = lv_label_create(s_cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);

    /* Position crosshair at corner 0 */
    position_crosshair(kCorners[0].x, kCorners[0].y, lv_color_hex(0xffffff));

    return scr;
}

/* ── Public API ──────────────────────────────────────────────────── */

namespace calibrate {

void launch() {
    /* Reset state */
    s_corner        = 0;
    s_win_count     = 0;
    s_win_head      = 0;
    s_stable_count  = 0;
    s_was_touched   = false;
    s_last_touch_ms = millis();
    s_last_sample_ms = 0;
    s_cal_running   = true;

    if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }

    s_scr = build_screen();
    screen_router::push(s_scr);
    lv_scr_load(s_scr);   /* force-active immediately — no animation gap where
                            * the launcher can receive a spurious startup touch */

    set_title(0);
    set_status("Hold steady...");

    /* LVGL timer drives the sampling loop — avoids blocking the main loop */
    s_timer = lv_timer_create(cal_timer_cb, SAMPLE_INTERVAL_MS, nullptr);

    LOG_I("cal", "Screen created — corner 0/4 (TL) — hold-to-settle calibration started");
    Serial.println("[cal] Screen created — corner 0/4 (TL)");
    Serial.printf("[cal] Thresholds: WINDOW=%d STABLE_CYCLES=%d STDDEV_MAX=%.0f\n",
                  WINDOW_SIZE, STABLE_CYCLES, STDDEV_THRESHOLD);
    Serial.println("[cal] Calibration started. Hold stylus on each crosshair until the ring fills.");
}

lv_obj_t* create_screen() {
    launch();
    return s_scr;
}

} // namespace calibrate

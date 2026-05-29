/**
 * calibrate.cpp — Hold-to-settle touch calibration screen.
 *
 * UX flow per corner:
 *   1. Display crosshair + progress arc at the target point.
 *   2. User presses and HOLDS the stylus on the target.
 *   3. Firmware samples raw XPT2046 readings at ~30 Hz.
 *   4. First 20 samples build the sliding window (arc 0%→50%).
 *   5. Once the window has ≥20 samples AND std-dev for both X and Y
 *      is < 30 ADC counts, a stability counter increments.
 *      15 consecutive stable samples fills arc 50%→100%.
 *      If std-dev spikes above 30, stability counter resets to 0
 *      and the arc decays back to 50%.
 *   6. On 100%: record mean, flash crosshair green, wait 300 ms, next corner.
 *   7. Release before 100%: reset state back to "Hold steady...".
 *   8. No touch for 60 s: show failure banner for 3 s, retry from corner 0.
 *
 * On any failure (timeout, sanity-check rejection, NVS error):
 *   - Shows a red "Calibration failed" banner with the reason for 3 s.
 *   - Logs a comprehensive diagnostic line.
 *   - Resets to corner 0 and retries.
 *   - Cancel button is the ONLY deliberate exit path (shows confirmation dialog).
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
#define WINDOW_SIZE        20        /* sliding window length          */
#define STABLE_CYCLES      15        /* consecutive stable cycles req. */
#define STDDEV_THRESHOLD   30.0f     /* ADC counts                    */
#define TIMEOUT_MS         60000     /* abandon corner if no touch for 60 s */
#define FAILURE_BANNER_MS  3000      /* show failure banner for 3 s   */

/* ── LVGL widget handles ─────────────────────────────────────────── */
static lv_obj_t* s_scr          = nullptr;
static lv_obj_t* s_title_lbl    = nullptr;
static lv_obj_t* s_sub_lbl      = nullptr;
static lv_obj_t* s_arc          = nullptr;
static lv_obj_t* s_status_lbl   = nullptr;
static lv_obj_t* s_h_line       = nullptr;   /* horizontal bar of crosshair */
static lv_obj_t* s_v_line       = nullptr;   /* vertical bar of crosshair   */
static lv_obj_t* s_cancel_btn   = nullptr;
static lv_obj_t* s_confirm_box  = nullptr;   /* cancel-confirmation modal   */

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

/* ── Forward declarations ────────────────────────────────────────── */

static void advance_corner();
static void finish_calibration();
static void fail_and_retry(const char* reason, int failed_corner);
static void reset_to_corner0();
static void do_cancel_pop();

/* ── Build the calibration screen ────────────────────────────────── */
/* Forward-declared so cancel_cb / confirm callbacks can reference it */
static lv_obj_t* build_screen();

/* ── Cancel confirmation callbacks ──────────────────────────────── */

static void confirm_yes_cb(lv_event_t* e) {
    /* User confirmed cancel — destroy modal and pop to launcher */
    if (s_confirm_box) {
        lv_obj_delete(s_confirm_box);
        s_confirm_box = nullptr;
    }
    do_cancel_pop();
}

static void confirm_no_cb(lv_event_t* e) {
    /* User chose "No" — dismiss modal, continue calibrating */
    if (s_confirm_box) {
        lv_obj_delete(s_confirm_box);
        s_confirm_box = nullptr;
    }
    /* Timer keeps running normally — the dialog just blocked input via the
     * s_confirm_box guard in cal_timer_cb. Nothing else to do. */
}

/* ── Cancel button callback ──────────────────────────────────────── */

static void cancel_cb(lv_event_t* e) {
    /* Don't open a second dialog if one is already showing */
    if (s_confirm_box) return;

    /* Build a small confirmation modal */
    s_confirm_box = lv_obj_create(s_scr);
    lv_obj_set_size(s_confirm_box, 260, 130);
    lv_obj_center(s_confirm_box);
    lv_obj_set_style_bg_color(s_confirm_box, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_color(s_confirm_box, lv_color_hex(0x888888), 0);
    lv_obj_set_style_border_width(s_confirm_box, 1, 0);
    lv_obj_set_style_radius(s_confirm_box, 8, 0);
    lv_obj_set_style_pad_all(s_confirm_box, 12, 0);

    lv_obj_t* msg = lv_label_create(s_confirm_box);
    lv_label_set_text(msg, "Cancel calibration?\nExisting cal will be kept.");
    lv_obj_set_style_text_color(msg, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg, 230);
    lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* yes_btn = lv_button_create(s_confirm_box);
    lv_obj_set_size(yes_btn, 90, 36);
    lv_obj_align(yes_btn, LV_ALIGN_BOTTOM_LEFT, 8, 0);
    lv_obj_set_style_bg_color(yes_btn, lv_color_hex(0xcc3333), 0);
    lv_obj_add_event_cb(yes_btn, confirm_yes_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* yes_lbl = lv_label_create(yes_btn);
    lv_label_set_text(yes_lbl, "Yes, cancel");
    lv_obj_center(yes_lbl);

    lv_obj_t* no_btn = lv_button_create(s_confirm_box);
    lv_obj_set_size(no_btn, 90, 36);
    lv_obj_align(no_btn, LV_ALIGN_BOTTOM_RIGHT, -8, 0);
    lv_obj_set_style_bg_color(no_btn, lv_color_hex(0x336633), 0);
    lv_obj_add_event_cb(no_btn, confirm_no_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* no_lbl = lv_label_create(no_btn);
    lv_label_set_text(no_lbl, "No, continue");
    lv_obj_center(no_lbl);
}

/* ── Actual pop-to-launcher (used by Cancel-confirmed path) ──────── */

static void do_cancel_pop() {
    s_cal_running = false;
    if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }
    LOG_W("cal", "User cancelled calibration");
    Serial.println("[cal] User cancelled — popping to launcher");
    screen_router::pop();
}

/* ── LVGL timer callback — called every SAMPLE_INTERVAL_MS ──────── */

static void cal_timer_cb(lv_timer_t* timer) {
    if (!s_cal_running) return;
    /* Don't sample while confirmation dialog is open */
    if (s_confirm_box) return;

    TFT_eSPI* tft = display::get_tft();
    uint16_t rx = 0, ry = 0;
    bool touched = tft->getTouchRaw(&rx, &ry);   /* RAW ADC — independent of NVS affine */
    uint32_t now = millis();

    /* Timeout: no touch for TIMEOUT_MS */
    if (touched) {
        s_last_touch_ms = now;
    } else if (now - s_last_touch_ms > TIMEOUT_MS) {
        Serial.printf("[cal] Corner %d TIMEOUT after 60s\n", s_corner);
        char reason[64];
        snprintf(reason, sizeof(reason), "Timeout — no touch on corner %d", s_corner);
        fail_and_retry(reason, s_corner);
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

                /* Wait for lift — raw API so it works before cal is applied */
                uint16_t dx, dy;
                while (tft->getTouchRaw(&dx, &dy)) { delay(10); }

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

/* ── Reset all cal state back to corner 0 ────────────────────────── */

static void reset_to_corner0() {
    s_corner        = 0;
    s_win_count     = 0;
    s_win_head      = 0;
    s_stable_count  = 0;
    s_was_touched   = false;
    s_last_touch_ms = millis();
    s_last_sample_ms = 0;

    /* Zero all corner accumulators */
    for (int i = 0; i < 4; i++) {
        s_corner_x[i] = 0;
        s_corner_y[i] = 0;
    }

    /* Restore cal screen widgets (may have been cleaned on success path) */
    if (!s_title_lbl || !lv_obj_is_valid(s_title_lbl)) {
        /* Screen was cleaned — rebuild it */
        if (s_scr && lv_obj_is_valid(s_scr)) {
            lv_obj_delete(s_scr);
        }
        s_scr = build_screen();
        screen_router::push_silent(s_scr);
        lv_scr_load(s_scr);
    }

    set_title(0);
    set_status("Hold steady...");
    set_arc(0);
    position_crosshair(kCorners[0].x, kCorners[0].y, lv_color_hex(0xffffff));
    lv_refr_now(NULL);
}

/* ── Show failure banner for FAILURE_BANNER_MS, then retry ──────── */

static void fail_and_retry(const char* reason, int failed_corner) {
    /* Stop the sampling timer while we show the banner */
    s_cal_running = false;
    if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }

    /* Collect whatever corner data we have for the diagnostic line */
    int32_t x_at_left = 0, x_at_right = 0, y_at_top = 0, y_at_bottom = 0;
    if (failed_corner >= 4) {
        /* We had all corners — show the raw-X affine (pre-swap, for reference) */
        x_at_left   = (s_corner_x[0] + s_corner_x[3]) / 2;
        x_at_right  = (s_corner_x[1] + s_corner_x[2]) / 2;
        y_at_top    = (s_corner_y[0] + s_corner_y[1]) / 2;
        y_at_bottom = (s_corner_y[3] + s_corner_y[2]) / 2;
    }

    Serial.printf("[cal] FAILURE corner=%d reason=\"%s\" "
                  "raw_tl=(%ld,%ld) raw_tr=(%ld,%ld) raw_br=(%ld,%ld) raw_bl=(%ld,%ld) "
                  "naive_x[%ld->%ld] naive_y[%ld->%ld]\n",
                  failed_corner, reason,
                  (long)s_corner_x[0], (long)s_corner_y[0],
                  (long)s_corner_x[1], (long)s_corner_y[1],
                  (long)s_corner_x[2], (long)s_corner_y[2],
                  (long)s_corner_x[3], (long)s_corner_y[3],
                  (long)x_at_left, (long)x_at_right,
                  (long)y_at_top,  (long)y_at_bottom);
    LOG_W("cal", "FAILURE: %s — restarting from corner 0", reason);
    Serial.printf("[cal] FAILURE: %s — restarting from corner 0\n", reason);

    /* Show red failure banner over the current screen */
    if (s_scr && lv_obj_is_valid(s_scr)) {
        /* Overlay a semi-transparent panel */
        lv_obj_t* panel = lv_obj_create(s_scr);
        lv_obj_set_size(panel, SCREEN_W - 40, 120);
        lv_obj_center(panel);
        lv_obj_set_style_bg_color(panel, lv_color_hex(0x440000), 0);
        lv_obj_set_style_border_color(panel, lv_color_hex(0xff0000), 0);
        lv_obj_set_style_border_width(panel, 2, 0);
        lv_obj_set_style_radius(panel, 8, 0);
        lv_obj_set_style_pad_all(panel, 10, 0);

        lv_obj_t* big_lbl = lv_label_create(panel);
        lv_label_set_text(big_lbl, "Calibration failed");
        lv_obj_set_style_text_color(big_lbl, lv_color_hex(0xff4444), 0);
        /* Make it large via font — use the built-in montserrat_20 if available */
        lv_obj_set_style_text_font(big_lbl, &lv_font_montserrat_20, 0);
        lv_obj_align(big_lbl, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t* reason_lbl = lv_label_create(panel);
        lv_label_set_text(reason_lbl, reason);
        lv_obj_set_style_text_color(reason_lbl, lv_color_hex(0xffaaaa), 0);
        lv_obj_set_style_text_align(reason_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(reason_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(reason_lbl, SCREEN_W - 80);
        lv_obj_align(reason_lbl, LV_ALIGN_BOTTOM_MID, 0, 0);

        lv_refr_now(NULL);

        /* Block for FAILURE_BANNER_MS, pumping LVGL */
        uint32_t banner_start = millis();
        while (millis() - banner_start < FAILURE_BANNER_MS) {
            lv_timer_handler();
            delay(20);
        }

        /* Remove the overlay panel */
        lv_obj_delete(panel);
    }

    /* Reset state and redraw corner 0 */
    reset_to_corner0();

    /* Restart the sampling timer */
    s_cal_running = true;
    s_timer = lv_timer_create(cal_timer_cb, SAMPLE_INTERVAL_MS, nullptr);

    Serial.println("[cal] Retry started — corner 0/4 (TL)");
}

/* ── Finish: validate, save, hot-apply ───────────────────────────── */

static void finish_calibration() {
    s_cal_running = false;
    if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }

    /* ── Diagnostic: dump all 4 corner raw values ── */
    Serial.printf("[cal] CORNERS raw_tl=(%ld,%ld) raw_tr=(%ld,%ld) raw_br=(%ld,%ld) raw_bl=(%ld,%ld)\n",
                  (long)s_corner_x[0], (long)s_corner_y[0],
                  (long)s_corner_x[1], (long)s_corner_y[1],
                  (long)s_corner_x[2], (long)s_corner_y[2],
                  (long)s_corner_x[3], (long)s_corner_y[3]);

    /* ── Axis detection ──────────────────────────────────────────────
     * Compute how much each raw channel moves horizontally (left→right)
     * and vertically (top→bottom).  Whichever raw channel has the larger
     * horizontal spread is the one that physically runs left-right — that
     * is the correct source for screen-X.
     *
     * Corners: 0=TL, 1=TR, 2=BR, 3=BL
     */
    int32_t dx_horiz = abs( ((s_corner_x[1] + s_corner_x[2]) / 2) -
                             ((s_corner_x[0] + s_corner_x[3]) / 2) );  /* raw_x spread L→R */
    int32_t dy_horiz = abs( ((s_corner_y[1] + s_corner_y[2]) / 2) -
                             ((s_corner_y[0] + s_corner_y[3]) / 2) );  /* raw_y spread L→R */
    int32_t dx_vert  = abs( ((s_corner_x[3] + s_corner_x[2]) / 2) -
                             ((s_corner_x[0] + s_corner_x[1]) / 2) );  /* raw_x spread T→B */
    int32_t dy_vert  = abs( ((s_corner_y[3] + s_corner_y[2]) / 2) -
                             ((s_corner_y[0] + s_corner_y[1]) / 2) );  /* raw_y spread T→B */

    /* axis_swap = true when raw_y varies more horizontally than raw_x does,
     * AND raw_x varies more vertically than raw_y — classic 90° mount. */
    bool axis_swap = (dy_horiz > dx_horiz) && (dx_vert > dy_vert);

    Serial.printf("[cal] AXIS_DETECT dx_horiz=%ld dy_horiz=%ld dx_vert=%ld dy_vert=%ld swap=%s\n",
                  (long)dx_horiz, (long)dy_horiz, (long)dx_vert, (long)dy_vert,
                  axis_swap ? "YES" : "NO");
    LOG_I("cal", "Axis detection: dx_horiz=%ld dy_horiz=%ld dx_vert=%ld dy_vert=%ld swap=%s",
          (long)dx_horiz, (long)dy_horiz, (long)dx_vert, (long)dy_vert,
          axis_swap ? "YES" : "NO");

    /* ── Build signed-delta affine endpoints ──────────────────────────
     * x_at_left  = mean of the "x-axis source" values at left-edge corners (TL, BL)
     * x_at_right = mean at right-edge corners (TR, BR)
     * y_at_top   = mean at top-edge corners (TL, TR)
     * y_at_bottom= mean at bottom-edge corners (BL, BR)
     *
     * With axis_swap, raw_y is the X source and raw_x is the Y source.
     * We do NOT enforce min<max — signed deltas handle inverted panels.
     */
    int32_t x_at_left, x_at_right, y_at_top, y_at_bottom;
    if (!axis_swap) {
        /* Normal orientation: raw_x runs horizontally, raw_y vertically */
        x_at_left   = (s_corner_x[0] + s_corner_x[3]) / 2;  /* TL.rx + BL.rx */
        x_at_right  = (s_corner_x[1] + s_corner_x[2]) / 2;  /* TR.rx + BR.rx */
        y_at_top    = (s_corner_y[0] + s_corner_y[1]) / 2;  /* TL.ry + TR.ry */
        y_at_bottom = (s_corner_y[3] + s_corner_y[2]) / 2;  /* BL.ry + BR.ry */
    } else {
        /* Swapped: raw_y runs horizontally, raw_x runs vertically */
        x_at_left   = (s_corner_y[0] + s_corner_y[3]) / 2;  /* TL.ry + BL.ry */
        x_at_right  = (s_corner_y[1] + s_corner_y[2]) / 2;  /* TR.ry + BR.ry */
        y_at_top    = (s_corner_x[0] + s_corner_x[1]) / 2;  /* TL.rx + TR.rx */
        y_at_bottom = (s_corner_x[3] + s_corner_x[2]) / 2;  /* BL.rx + BR.rx */
    }

    Serial.printf("[cal] AFFINE x_at_left=%ld x_at_right=%ld y_at_top=%ld y_at_bottom=%ld\n",
                  (long)x_at_left, (long)x_at_right,
                  (long)y_at_top,  (long)y_at_bottom);
    LOG_I("cal", "Affine: x[%ld->%ld] y[%ld->%ld] (signed deltas; negative = inverted axis)",
          (long)x_at_left, (long)x_at_right, (long)y_at_top, (long)y_at_bottom);

    /* ── Sanity check: spread must be at least 500 counts per axis ── */
    int32_t x_spread = abs(x_at_right - x_at_left);
    int32_t y_spread = abs(y_at_bottom - y_at_top);
    if (x_spread < 500 || y_spread < 500) {
        Serial.printf("[cal] REJECT: spread too small — x_spread=%ld y_spread=%ld (need >=500 each)\n",
                      (long)x_spread, (long)y_spread);
        char reason[120];
        snprintf(reason, sizeof(reason),
                 "Bad values: x_spread=%ld y_spread=%ld (need >=500)",
                 (long)x_spread, (long)y_spread);
        fail_and_retry(reason, 4);
        return;
    }

    /* ── Sanity check: stuck ADC ─────────────────────────────────── */
    if (x_at_left == 0 || x_at_right == 4095 ||
        y_at_top  == 0 || y_at_bottom == 4095 ||
        x_at_right == 0 || x_at_left == 4095 ||
        y_at_bottom == 0 || y_at_top == 4095) {
        Serial.printf("[cal] REJECT: stuck ADC value (0 or 4095) in affine endpoints\n");
        fail_and_retry("Bad values: stuck ADC at 0 or 4095", 4);
        return;
    }

    Serial.println("[cal] SAVE: ok — all sanity checks passed");

    /* ── Save to NVS ─────────────────────────────────────────────── */
    /* cal_x_min / cal_x_max reinterpreted as x_at_left / x_at_right.
     * cal_y_min / cal_y_max reinterpreted as y_at_top / y_at_bottom.
     * axis_swap stored as a new u8 key. NVS schema is otherwise unchanged. */
    nvs_store::put_i32("touch", "cal_x_min",  x_at_left);
    nvs_store::put_i32("touch", "cal_x_max",  x_at_right);
    nvs_store::put_i32("touch", "cal_y_min",  y_at_top);
    nvs_store::put_i32("touch", "cal_y_max",  y_at_bottom);
    nvs_store::put_u8 ("touch", "axis_swap",  axis_swap ? 1 : 0);
    nvs_store::put_u8 ("touch", "calibrated", 1);

    /* Readback verify (x values only — sufficient to detect NVS write failure) */
    int32_t rb_xl = nvs_store::get_i32("touch", "cal_x_min", -1);
    int32_t rb_xr = nvs_store::get_i32("touch", "cal_x_max", -1);
    if (rb_xl != x_at_left || rb_xr != x_at_right) {
        Serial.printf("[cal] REJECT: NVS readback mismatch (wrote x[%ld->%ld] read x[%ld->%ld])\n",
                      (long)x_at_left, (long)x_at_right, (long)rb_xl, (long)rb_xr);
        fail_and_retry("NVS save failed", 4);
        return;
    }
    Serial.println("[cal] NVS write ok");

    /* Hot-apply immediately */
    touch::apply_cal(x_at_left, x_at_right, y_at_top, y_at_bottom, axis_swap);

    LOG_I("cal", "Calibration saved and applied");
    Serial.printf("[cal] New cal: swap=%s x[%ld->%ld] y[%ld->%ld]\n",
                  axis_swap ? "YES" : "NO",
                  (long)x_at_left, (long)x_at_right,
                  (long)y_at_top,  (long)y_at_bottom);

    /* Show success message */
    lv_obj_clean(s_scr);
    lv_obj_t* lbl = lv_label_create(s_scr);
    lv_label_set_text(lbl, "Calibration saved!\nTap anywhere to continue");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x00ff00), 0);
    lv_obj_center(lbl);
    lv_refr_now(NULL);

    /* Null out widget handles — screen was cleaned */
    s_title_lbl  = nullptr;
    s_sub_lbl    = nullptr;
    s_arc        = nullptr;
    s_status_lbl = nullptr;
    s_h_line     = nullptr;
    s_v_line     = nullptr;
    s_cancel_btn = nullptr;
    s_confirm_box = nullptr;

    /* Wait for any tap — use getTouchRaw so no TFT_eSPI internal cal needed */
    TFT_eSPI* tft = display::get_tft();
    uint16_t dx, dy;
    uint32_t wait_start = millis();
    while (!tft->getTouchRaw(&dx, &dy) && (millis() - wait_start < 15000)) {
        lv_timer_handler();
        delay(20);
    }
    while (tft->getTouchRaw(&dx, &dy)) { delay(20); }

    screen_router::pop();
}

/* ── Build the calibration screen ────────────────────────────────── */

static lv_obj_t* build_screen() {
    Serial.println("[cal] build_screen() entered");
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

    int child_count = lv_obj_get_child_count(scr);
    Serial.printf("[cal] build_screen() returning scr=%p with %d children\n",
                  (void*)scr, child_count);
    return scr;
}

/* ── Public API ──────────────────────────────────────────────────── */

namespace calibrate {

void launch() {
    Serial.println("[cal] launch() step 1: resetting state");
    /* Reset state */
    s_corner        = 0;
    s_win_count     = 0;
    s_win_head      = 0;
    s_stable_count  = 0;
    s_was_touched   = false;
    s_last_touch_ms = millis();
    s_last_sample_ms = 0;
    s_cal_running   = true;
    s_confirm_box   = nullptr;

    for (int i = 0; i < 4; i++) { s_corner_x[i] = 0; s_corner_y[i] = 0; }

    if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }

    Serial.println("[cal] launch() step 2: building screen");
    s_scr = build_screen();
    /* Use push_silent to register in router stack WITHOUT queuing a lv_scr_load_anim —
     * calling push() first then lv_scr_load() causes LVGL 9 to ignore the second load
     * call because the fade animation is already pending. push_silent lets us own
     * the actual screen switch via lv_scr_load(). */
    Serial.printf("[cal] launch() step 3: calling screen_router::push_silent(scr=%p) active_before=%p\n",
                  (void*)s_scr, (void*)lv_scr_act());
    screen_router::push_silent(s_scr);
    Serial.printf("[cal] launch() step 4: after push_silent active=%p\n", (void*)lv_scr_act());
    lv_scr_load(s_scr);   /* immediate switch — no animation, no conflict */
    Serial.printf("[cal] launch() step 5: after lv_scr_load active=%p (cal_scr=%p match=%s)\n",
                  (void*)lv_scr_act(), (void*)s_scr,
                  (lv_scr_act() == s_scr) ? "YES" : "NO");

    set_title(0);
    set_status("Hold steady...");

    /* LVGL timer drives the sampling loop — avoids blocking the main loop */
    s_timer = lv_timer_create(cal_timer_cb, SAMPLE_INTERVAL_MS, nullptr);

    LOG_I("cal", "Screen created — corner 0/4 (TL) — hold-to-settle calibration started");
    Serial.println("[cal] Screen created — corner 0/4 (TL)");
    Serial.printf("[cal] Thresholds: WINDOW=%d STABLE_CYCLES=%d STDDEV_MAX=%.0f TIMEOUT=%ds\n",
                  WINDOW_SIZE, STABLE_CYCLES, STDDEV_THRESHOLD, TIMEOUT_MS / 1000);
    Serial.println("[cal] Calibration started. Hold stylus on each crosshair until the ring fills.");
    Serial.println("[cal] On failure: red banner shown for 3s, then retry from corner 0.");
    Serial.println("[cal] Cancel button (bottom-right) shows confirmation before exiting.");
}

lv_obj_t* create_screen() {
    launch();
    return s_scr;
}

} // namespace calibrate

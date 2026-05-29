#include "touch.h"
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
 */

/* ── Defaults from PHASE1.md §6 ─────────────────────────────────── */
static const int32_t kDefaultXMin = 275;
static const int32_t kDefaultXMax = 3620;
static const int32_t kDefaultYMin = 264;
static const int32_t kDefaultYMax = 3532;

/* ── Calibration state ───────────────────────────────────────────── */
static int32_t s_x_min, s_x_max, s_y_min, s_y_max;

/* Shared TFT instance for getTouch() — same SPI bus as display.
 * NOTE: lv_tft_espi_create() (called in display::init) already called
 * tft.begin() on its internal TFT_eSPI instance.  We must NOT call
 * s_tft.begin() a second time because that re-initialises the ST7796
 * controller over SPI and leaves the bus in an inconsistent state.
 * Instead we skip begin() and use s_tft solely for getTouch() / SPI
 * touch reads, which share the same hardware SPI peripheral.
 */
static TFT_eSPI s_tft;

/* ── LVGL indev read callback ────────────────────────────────────── */
static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    uint16_t raw_x = 0, raw_y = 0;
    bool touched = s_tft.getTouch(&raw_x, &raw_y);

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
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ── 4-corner calibration ────────────────────────────────────────── */

/**
 * Collect N raw samples for a given corner.
 * Discards min + max, averages the remaining 3.
 * @param n        Number of samples (5 recommended)
 * @param out_x    Averaged X
 * @param out_y    Averaged Y
 */
static void collect_corner(const char* prompt_text, int n, int32_t* out_x, int32_t* out_y) {
    /* Create a full-screen black overlay with prompt */
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

    lv_refr_now(NULL);  // force render before blocking

    /* Wait for touch down */
    uint16_t rx[5], ry[5];
    int collected = 0;

    while (collected < n) {
        uint16_t tx, ty;
        if (s_tft.getTouch(&tx, &ty)) {
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

    /* Wait for release — must pass valid pointers; getTouch writes to *x/*y */
    uint16_t rx_tmp, ry_tmp;
    while (s_tft.getTouch(&rx_tmp, &ry_tmp)) {
        delay(20);
    }
    delay(200);

    /* Discard min + max, average survivors */
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
        /* Fallback: plain average */
        sx = sy = 0;
        for (int i = 0; i < n; i++) { sx += rx[i]; sy += ry[i]; }
        *out_x = sx / n;
        *out_y = sy / n;
    }

    lv_obj_del(overlay);
}

void touch::run_calibration() {
    LOG_I("touch", "Starting 4-corner calibration");

    int32_t tl_x, tl_y;  /* top-left */
    int32_t tr_x, tr_y;  /* top-right */
    int32_t bl_x, bl_y;  /* bottom-left */
    int32_t br_x, br_y;  /* bottom-right */

    collect_corner("Tap TOP-LEFT corner",     5, &tl_x, &tl_y);
    collect_corner("Tap TOP-RIGHT corner",    5, &tr_x, &tr_y);
    collect_corner("Tap BOTTOM-LEFT corner",  5, &bl_x, &bl_y);
    collect_corner("Tap BOTTOM-RIGHT corner", 5, &br_x, &br_y);

    /* Derive min/max from 4-corner samples */
    s_x_min = (tl_x + bl_x) / 2;
    s_x_max = (tr_x + br_x) / 2;
    s_y_min = (tl_y + tr_y) / 2;
    s_y_max = (bl_y + br_y) / 2;

    /* Persist to NVS */
    nvs_store::put_i32("touch", "cal_x_min", s_x_min);
    nvs_store::put_i32("touch", "cal_x_max", s_x_max);
    nvs_store::put_i32("touch", "cal_y_min", s_y_min);
    nvs_store::put_i32("touch", "cal_y_max", s_y_max);
    nvs_store::put_u8("touch", "calibrated", 1);

    LOG_I("touch", "Cal done: x[%ld..%ld] y[%ld..%ld]",
          (long)s_x_min, (long)s_x_max, (long)s_y_min, (long)s_y_max);

    /* Show confirmation */
    lv_obj_t* msg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(msg, 280, 80);
    lv_obj_center(msg);
    lv_obj_t* lbl = lv_label_create(msg);
    lv_label_set_text(lbl, "Calibration saved!\nTouch to continue");
    lv_obj_center(lbl);
    lv_refr_now(NULL);

    /* Wait for any touch to dismiss */
    uint16_t dx, dy;
    while (!s_tft.getTouch(&dx, &dy)) { delay(20); }
    while (s_tft.getTouch(&dx, &dy))  { delay(20); }

    lv_obj_del(msg);
}

namespace touch {

void init() {
    // DO NOT call s_tft.begin() here.
    // display::init() → lv_tft_espi_create() already called begin() on the
    // shared SPI bus and initialised the ST7796 controller.  A second begin()
    // re-asserts RST and re-sends init commands, corrupting the display and
    // leaving SPI transactions in an inconsistent state that causes crashes
    // when touch_read_cb fires during an LVGL flush.  The s_tft instance is
    // used only for getTouch() (SPI touch reads) which work without begin().

    /* Load calibration from NVS */
    uint8_t cal = nvs_store::get_u8("touch", "calibrated", 0);
    if (cal) {
        s_x_min = nvs_store::get_i32("touch", "cal_x_min", kDefaultXMin);
        s_x_max = nvs_store::get_i32("touch", "cal_x_max", kDefaultXMax);
        s_y_min = nvs_store::get_i32("touch", "cal_y_min", kDefaultYMin);
        s_y_max = nvs_store::get_i32("touch", "cal_y_max", kDefaultYMax);
        LOG_I("touch", "Loaded cal x[%ld..%ld] y[%ld..%ld]",
              (long)s_x_min, (long)s_x_max, (long)s_y_min, (long)s_y_max);
    } else {
        /* First boot — use defaults until calibration completes */
        s_x_min = kDefaultXMin;
        s_x_max = kDefaultXMax;
        s_y_min = kDefaultYMin;
        s_y_max = kDefaultYMax;
        LOG_I("touch", "No cal in NVS — running calibration");
        run_calibration();
    }

    /* Register LVGL input device */
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
}

} // namespace touch

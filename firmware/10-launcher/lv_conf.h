/**
 * lv_conf.h — LVGL 9.2.2 configuration for EspScreen Phase 1
 *
 * IMPORTANT: All LVGL settings live here. Do NOT duplicate in platformio.ini -D flags.
 * Only LV_CONF_INCLUDE_SIMPLE is set in build_flags to point LVGL at this file.
 *
 * Key changes vs template defaults:
 *   - LV_COLOR_DEPTH 16 (RGB565, matches ST7796)
 *   - LV_MEM_SIZE 48 KB (monitor with esp_get_free_heap_size; reduce to 32 KB if needed)
 *   - LV_USE_TFT_ESPI 0 (display.cpp owns TFT_eSPI directly via custom flush_cb)
 *   - LV_FONT_MONTSERRAT_14 + 20 enabled; set as default
 *   - Disabled widgets: ARC, CHART, TABLE, CALENDAR, SPINBOX, SPAN, METER (save flash)
 */

#pragma once
#define LV_CONF_H

/* ── Color ──────────────────────────────────────────────────────────── */
#define LV_COLOR_DEPTH          16   /* 16-bit RGB565 to match ST7796 */

/* ── Memory ─────────────────────────────────────────────────────────── */
#define LV_MEM_SIZE             (48U * 1024U)  /* 48 KB LVGL heap */

/* ── Display driver ─────────────────────────────────────────────────── */
#define LV_USE_TFT_ESPI         0   /* display.cpp owns TFT_eSPI; custom flush_cb */

/* ── Refresh ─────────────────────────────────────────────────────────  */
#define LV_DEF_REFR_PERIOD      16  /* ~60 fps target (ms) */

/* ── Logging ─────────────────────────────────────────────────────────  */
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1   /* use printf (Serial via Arduino) */

/* ── Themes ──────────────────────────────────────────────────────────  */
#define LV_USE_THEME_DEFAULT    1
#define LV_USE_THEME_MONO       0
#define LV_USE_THEME_SIMPLE     0

/* ── Fonts ───────────────────────────────────────────────────────────  */
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

/* ── Widgets: enabled ────────────────────────────────────────────────  */
#define LV_USE_BUTTON           1
#define LV_USE_LABEL            1
#define LV_USE_IMG              1
#define LV_USE_GRID             1
#define LV_USE_FLEX             1
#define LV_USE_MSGBOX           1
#define LV_USE_TEXTAREA         1
#define LV_USE_KEYBOARD         1

/* ── Widgets: disabled (reduce flash ~40 KB) ─────────────────────────  */
/* NOTE: LV_USE_SPINNER depends on LV_USE_ARC. Disable both together.   */
#define LV_USE_ARC              0
#define LV_USE_SPINNER          0
#define LV_USE_CHART            0
#define LV_USE_TABLE            0
#define LV_USE_CALENDAR         0
#define LV_USE_SPINBOX          0
#define LV_USE_SPAN             0
#define LV_USE_METER            0

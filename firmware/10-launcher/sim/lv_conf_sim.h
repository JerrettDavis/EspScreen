/**
 * lv_conf_sim.h — Host (native) LVGL configuration for the ESPScreen simulator.
 *
 * Phase 1 goal: compile LVGL + the real launcher screen code on the host and
 * render it headlessly to an in-memory framebuffer.
 *
 * This file INHERITS the device lv_conf.h (../lv_conf.h) verbatim, then
 * overrides ONLY the handful of settings that must differ on the host:
 *   - LV_MEM_SIZE: the device runs in a 48 KB heap; on the host we have no
 *     such constraint and a full-screen render benefits from a larger pool.
 *   - LV_USE_LOG / LV_LOG_PRINTF: route LVGL logs to stdout for debugging.
 *
 * CRITICAL: do NOT change LV_COLOR_DEPTH, the enabled fonts, or
 * LV_DEF_REFR_PERIOD here — later phases compare host renders against device
 * renders pixel-for-pixel, and any of those would break that contract.
 *
 * LVGL is pointed at this file on the native build via:
 *     -D LV_CONF_INCLUDE_SIMPLE
 *     -D LV_CONF_PATH="${PROJECT_DIR}/sim/lv_conf_sim.h"
 * The device build keeps using the root lv_conf.h unchanged.
 */
#pragma once

/* The root device conf #defines LV_CONF_H. Pull it in first so we get every
 * font/widget/color setting, then selectively override below. The relative
 * path resolves from this file's directory (sim/) up to the project root. */
#include "../lv_conf.h"

/* ── Host-only overrides ─────────────────────────────────────────────────── */

#undef  LV_MEM_SIZE
#define LV_MEM_SIZE   (2 * 1024 * 1024)   /* 2 MB — plenty for a host render */

#undef  LV_USE_LOG
#define LV_USE_LOG    1

#undef  LV_LOG_PRINTF
#define LV_LOG_PRINTF 1

/* DO NOT change LV_COLOR_DEPTH, fonts, or LV_DEF_REFR_PERIOD — pixel-exactness
 * with the device build depends on them staying identical. */

/**
 * sim_display.h — shared in-memory LVGL display for the host simulator.
 *
 * Factored out of P1's headless main so both the headless renderer and the
 * Unity validator harness create the SAME 320x480 memory display (no GDI/SDL).
 * The flush_cb blits the partial px_map into a full RGB565-LE framebuffer the
 * caller can read back (e.g. to encode a PNG).
 */
#pragma once

#include <lvgl.h>
#include <cstdint>

#define SIM_DISP_W 320
#define SIM_DISP_H 480

/** Call lv_init() exactly once (idempotent across multiple callers). */
void sim_lv_init_once(void);

/** Create (once) the shared in-memory 320x480 display and return it.
 *  Subsequent calls return the same display. Installs a host tick source and
 *  the framebuffer-blit flush callback. */
lv_display_t* sim_display_get(void);

/** Pointer to the RGB565-LE framebuffer (SIM_DISP_W * SIM_DISP_H uint16_t). */
const uint16_t* sim_display_framebuffer(void);

/** Zero the framebuffer (black) — call before a render you want to measure. */
void sim_display_clear(void);

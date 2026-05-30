/**
 * sim_display.cpp — shared in-memory LVGL display implementation.
 */
#include "sim_display.h"
#include <Arduino.h>   /* millis() for the host tick source */
#include <cstring>

static uint16_t      g_framebuffer[SIM_DISP_W * SIM_DISP_H];
static lv_display_t* g_disp     = nullptr;
static bool          g_inited   = false;

static uint32_t sim_host_tick_cb(void) {
    return (uint32_t)millis();
}

static void sim_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const uint16_t* src = reinterpret_cast<const uint16_t*>(px_map);
    const int32_t aw = area->x2 - area->x1 + 1;
    for (int32_t y = area->y1; y <= area->y2; ++y) {
        uint16_t* dst_row = &g_framebuffer[y * SIM_DISP_W + area->x1];
        std::memcpy(dst_row, src, (size_t)aw * sizeof(uint16_t));
        src += aw;
    }
    lv_display_flush_ready(disp);
}

void sim_lv_init_once(void) {
    if (g_inited) return;
    lv_init();
    lv_tick_set_cb(sim_host_tick_cb);
    g_inited = true;
}

lv_display_t* sim_display_get(void) {
    sim_lv_init_once();
    if (g_disp) return g_disp;

    g_disp = lv_display_create(SIM_DISP_W, SIM_DISP_H);
    /* Two full-screen draw buffers, PARTIAL render mode (matches device). */
    static uint16_t draw_buf1[SIM_DISP_W * SIM_DISP_H];
    static uint16_t draw_buf2[SIM_DISP_W * SIM_DISP_H];
    lv_display_set_buffers(g_disp, draw_buf1, draw_buf2,
                           sizeof(draw_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(g_disp, sim_flush_cb);
    return g_disp;
}

const uint16_t* sim_display_framebuffer(void) {
    return g_framebuffer;
}

void sim_display_clear(void) {
    std::memset(g_framebuffer, 0, sizeof(g_framebuffer));
}

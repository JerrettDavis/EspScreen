/**
 * sim_headless_main.cpp — headless PNG renderer (P1 payoff, P2-refactored).
 *
 * Compiles LVGL + the REAL screen code against the host stub layer, renders a
 * screen headlessly into the shared in-memory RGB565 framebuffer (sim_display),
 * converts RGB565 → RGB888, and writes a PNG under sim/_artifacts/.
 *
 * Usage: program.exe [screen_id]   (default: "launcher")
 * Prints the non-background pixel count and exits 0 on success.
 */
#define SIM_PNG_WRITE_IMPL          /* this TU owns the stb implementation */

#include <lvgl.h>
#include <Arduino.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#include "sim_registry.h"
#include "sim_display.h"
#include "png_write.h"
#include "../src/ui/theme.h"   /* ui_theme::apply() — installs the dark theme + styles */

/* ── RGB565-LE → RGB888 expansion (bit-replication for full-range scaling) ─── */
static void rgb565_to_rgb888(const uint16_t* fb, int w, int h, std::vector<uint8_t>& out) {
    out.resize((size_t)w * h * 3);
    for (int i = 0; i < w * h; ++i) {
        uint16_t p = fb[i];
        uint8_t r5 = (p >> 11) & 0x1F;
        uint8_t g6 = (p >> 5)  & 0x3F;
        uint8_t b5 =  p        & 0x1F;
        out[i * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
        out[i * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
        out[i * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
    }
}

/* During `pio test`, the Unity runner provides main(); exclude ours to avoid a
 * duplicate-symbol clash (both the headless renderer and the test harness link
 * the same build_src_filter sources). */
#ifndef PIO_UNIT_TESTING
int main(int argc, char** argv) {
    const char* want = (argc > 1) ? argv[1] : "launcher";
    Serial.println("[sim] headless render");

    lv_display_t* disp = sim_display_get();   /* lv_init + memory display */

    /* Install the ESPScreen design-system theme + custom styles (device does
     * this in main.cpp; without it LVGL falls back to its default LIGHT theme). */
    ui_theme::apply();

    sim_display_clear();

    /* Find the requested screen in the registry. */
    const sim_screen_t* sel = nullptr;
    for (int i = 0; i < sim_registry_count; ++i) {
        if (std::strcmp(sim_registry[i].id, want) == 0) { sel = &sim_registry[i]; break; }
    }
    if (!sel) {
        std::fprintf(stderr, "[sim] unknown screen id '%s'\n", want);
        return 1;
    }

    lv_obj_t* root = sel->create();
    if (!root) {
        std::fprintf(stderr, "[sim] create() returned NULL for '%s'\n", want);
        return 1;
    }
    lv_obj_update_layout(root);
    lv_screen_load(root);

    /* Pump enough to force a full render into the framebuffer. */
    for (int i = 0; i < 10; ++i) {
        lv_refr_now(disp);
        lv_timer_handler();
    }

    const uint16_t* fb = sim_display_framebuffer();
    /* BG_BASE 0x0D1117 in RGB565 — "non-background" = actual UI content. */
    const uint16_t bg565 = (uint16_t)(((0x0D >> 3) << 11) |
                                      ((0x11 >> 2) << 5)  |
                                       (0x17 >> 3));
    long nonbg = 0, nonblack = 0;
    for (int i = 0; i < SIM_DISP_W * SIM_DISP_H; ++i) {
        if (fb[i] != bg565) nonbg++;
        if (fb[i] != 0)     nonblack++;
    }

    std::vector<uint8_t> rgb;
    rgb565_to_rgb888(fb, SIM_DISP_W, SIM_DISP_H, rgb);

    char out_path[128];
    std::snprintf(out_path, sizeof(out_path), "sim/_artifacts/%s.png", want);
    if (!sim_write_png_rgb888(out_path, SIM_DISP_W, SIM_DISP_H, rgb.data())) {
        std::fprintf(stderr, "[sim] PNG write FAILED: %s\n", out_path);
        return 1;
    }

    std::printf("[sim] rendered %dx%d '%s' -> %s\n", SIM_DISP_W, SIM_DISP_H, want, out_path);
    std::printf("[sim] non-background pixels: %ld  (non-black: %ld)\n", nonbg, nonblack);
    std::printf("[sim] OK\n");
    return 0;
}
#endif /* PIO_UNIT_TESTING */

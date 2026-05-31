/**
 * sim_headless_main.cpp — headless PNG renderer + golden pixel-diff tool.
 *
 * Compiles LVGL + the REAL screen code against the host stub layer, renders
 * screens headlessly into the shared in-memory RGB565 framebuffer (sim_display),
 * converts RGB565 → RGB888, and either writes PNGs or pixel-diffs them against
 * committed goldens.
 *
 * Modes (argv):
 *   (no args)            render "launcher" to sim/_artifacts/launcher.png
 *   <screen_id>          render that screen to sim/_artifacts/<id>.png
 *   --all                render ALL registry screens to sim/_artifacts/<id>.png
 *   --goldens            (re)WRITE ALL goldens to sim/_goldens/<id>.png
 *   --update-goldens     alias for --goldens
 *   --diff               compare ALL screens against sim/_goldens/<id>.png
 *                        (EXACT match, 0 tolerance). On mismatch: writes
 *                        sim/_artifacts/<id>.actual.png, prints first differing
 *                        pixels, and exits non-zero.
 *
 * Excluded from the Unity test build via PIO_UNIT_TESTING (that build provides
 * its own main()).
 */
#define SIM_PNG_WRITE_IMPL          /* this TU owns the stb implementations */

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

static constexpr int RGB_BYTES = SIM_DISP_W * SIM_DISP_H * 3;

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

#ifndef PIO_UNIT_TESTING

/* One-time LVGL + display + theme setup (idempotent). */
static lv_display_t* g_disp = nullptr;
static void ensure_env() {
    if (g_disp) return;
    g_disp = sim_display_get();   /* lv_init + memory display */
    ui_theme::apply();            /* dark theme + custom styles (matches device) */
}

/* Render one screen by registry index into `out` (RGB888). Returns false on
 * a NULL create(). Leaves a fresh blank screen loaded afterwards so the next
 * render starts clean and the just-built tree can be freed. */
static bool render_index(int idx, std::vector<uint8_t>& out) {
    ensure_env();
    sim_display_clear();

    lv_obj_t* root = sim_registry[idx].create();
    if (!root) {
        std::fprintf(stderr, "[sim] create() returned NULL for '%s'\n", sim_registry[idx].id);
        return false;
    }
    lv_obj_update_layout(root);
    lv_screen_load(root);

    for (int i = 0; i < 10; ++i) {     /* force a full render into the framebuffer */
        lv_refr_now(g_disp);
        lv_timer_handler();
    }

    rgb565_to_rgb888(sim_display_framebuffer(), SIM_DISP_W, SIM_DISP_H, out);

    /* Detach + delete this root so per-screen renders don't accumulate. */
    lv_obj_t* blank = lv_obj_create(NULL);
    lv_screen_load(blank);
    lv_obj_delete(root);
    return true;
}

static int find_index(const char* id) {
    for (int i = 0; i < sim_registry_count; ++i)
        if (std::strcmp(sim_registry[i].id, id) == 0) return i;
    return -1;
}

/* ── render a single screen to sim/_artifacts/<id>.png ──────────────────────── */
static int mode_render_one(const char* id) {
    int idx = find_index(id);
    if (idx < 0) { std::fprintf(stderr, "[sim] unknown screen id '%s'\n", id); return 1; }

    std::vector<uint8_t> rgb;
    if (!render_index(idx, rgb)) return 1;

    char path[160];
    std::snprintf(path, sizeof(path), "sim/_artifacts/%s.png", id);
    if (!sim_write_png_rgb888(path, SIM_DISP_W, SIM_DISP_H, rgb.data())) {
        std::fprintf(stderr, "[sim] PNG write FAILED: %s\n", path);
        return 1;
    }
    std::printf("[sim] rendered %dx%d '%s' -> %s\n", SIM_DISP_W, SIM_DISP_H, id, path);
    return 0;
}

/* ── render all screens to a directory ──────────────────────────────────────── */
static int mode_render_all(const char* dir) {
    int rc = 0;
    for (int i = 0; i < sim_registry_count; ++i) {
        std::vector<uint8_t> rgb;
        if (!render_index(i, rgb)) { rc = 1; continue; }
        char path[160];
        std::snprintf(path, sizeof(path), "%s/%s.png", dir, sim_registry[i].id);
        if (!sim_write_png_rgb888(path, SIM_DISP_W, SIM_DISP_H, rgb.data())) {
            std::fprintf(stderr, "[sim] PNG write FAILED: %s\n", path);
            rc = 1; continue;
        }
        std::printf("[sim] wrote %s\n", path);
    }
    return rc;
}

/* ── diff all screens against committed goldens (EXACT match) ────────────────── */
static int mode_diff() {
    int total_mismatch = 0;
    for (int i = 0; i < sim_registry_count; ++i) {
        const char* id = sim_registry[i].id;
        std::vector<uint8_t> rgb;
        if (!render_index(i, rgb)) { total_mismatch++; continue; }

        char gpath[160];
        std::snprintf(gpath, sizeof(gpath), "sim/_goldens/%s.png", id);
        int gw = 0, gh = 0;
        uint8_t* golden = sim_load_png_rgb888(gpath, &gw, &gh);
        if (!golden) {
            std::printf("[diff] %-16s MISSING GOLDEN (%s) — run --goldens\n", id, gpath);
            total_mismatch++;
            char apath[160];
            std::snprintf(apath, sizeof(apath), "sim/_artifacts/%s.actual.png", id);
            sim_write_png_rgb888(apath, SIM_DISP_W, SIM_DISP_H, rgb.data());
            continue;
        }
        if (gw != SIM_DISP_W || gh != SIM_DISP_H) {
            std::printf("[diff] %-16s SIZE MISMATCH golden=%dx%d expected=%dx%d\n",
                        id, gw, gh, SIM_DISP_W, SIM_DISP_H);
            total_mismatch++;
            sim_free_png(golden);
            continue;
        }

        /* Exact per-pixel compare. */
        long diff_px = 0;
        int first[8][2]; int nfirst = 0;
        for (int p = 0; p < SIM_DISP_W * SIM_DISP_H; ++p) {
            const uint8_t* a = &rgb[p * 3];
            const uint8_t* b = &golden[p * 3];
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) {
                if (nfirst < 8) {
                    first[nfirst][0] = p % SIM_DISP_W;
                    first[nfirst][1] = p / SIM_DISP_W;
                    nfirst++;
                }
                diff_px++;
            }
        }
        sim_free_png(golden);

        if (diff_px == 0) {
            std::printf("[diff] %-16s OK (exact)\n", id);
        } else {
            std::printf("[diff] %-16s MISMATCH: %ld differing pixel(s). first:", id, diff_px);
            for (int k = 0; k < nfirst; ++k) std::printf(" (%d,%d)", first[k][0], first[k][1]);
            std::printf("\n");
            char apath[160];
            std::snprintf(apath, sizeof(apath), "sim/_artifacts/%s.actual.png", id);
            sim_write_png_rgb888(apath, SIM_DISP_W, SIM_DISP_H, rgb.data());
            std::printf("[diff] %-16s wrote actual -> %s\n", id, apath);
            total_mismatch++;
        }
    }

    if (total_mismatch == 0) {
        std::printf("[diff] ALL %d screens match goldens exactly.\n", sim_registry_count);
        return 0;
    }
    std::printf("[diff] FAILED: %d screen(s) differ from goldens.\n", total_mismatch);
    return 1;
}

int main(int argc, char** argv) {
    const char* arg = (argc > 1) ? argv[1] : nullptr;

    if (!arg) {
        return mode_render_one("launcher");
    }
    if (std::strcmp(arg, "--all") == 0) {
        std::printf("[sim] rendering all %d screens to sim/_artifacts/\n", sim_registry_count);
        return mode_render_all("sim/_artifacts");
    }
    if (std::strcmp(arg, "--goldens") == 0 || std::strcmp(arg, "--update-goldens") == 0) {
        std::printf("[sim] (RE)WRITING all %d goldens to sim/_goldens/\n", sim_registry_count);
        return mode_render_all("sim/_goldens");
    }
    if (std::strcmp(arg, "--diff") == 0) {
        std::printf("[sim] golden pixel-diff over all %d screens (exact match)\n", sim_registry_count);
        return mode_diff();
    }
    /* Otherwise treat the arg as a single screen id. */
    return mode_render_one(arg);
}
#endif /* PIO_UNIT_TESTING */

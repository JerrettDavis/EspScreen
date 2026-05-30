/**
 * sim_main.cpp — interactive desktop simulator (Phase 3).
 *
 * Renders the REAL ESPScreen screens in a Windows GDI window using LVGL's
 * vendored Windows backend (lv_windows_create_display / acquire_pointer_indev).
 * Mouse acts as touch; tapping launcher tiles drives the REAL screen_router
 * navigation. A starting screen can be chosen with --screen=<id>.
 *
 * Reuses: sim_registry (the 9 real screens), the sim/stubs host layer, and the
 * screen_validate rule engine (run after each explicit screen load so the sim
 * doubles as a live layout checker).
 *
 * This file is compiled ONLY into [env:sim]. The headless renderer's main() and
 * the Unity harness are excluded from that env, so there is exactly one main().
 */
#include <lvgl.h>
#include <Arduino.h>          /* host millis() for the LVGL tick source */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <clocale>

/* The Windows GDI backend is gated on LV_USE_WINDOWS (set by -D in [env:sim]). */
#include "../.pio/libdeps/esp32dev/lvgl/src/drivers/windows/lv_windows_display.h"
#include "../.pio/libdeps/esp32dev/lvgl/src/drivers/windows/lv_windows_input.h"

#include "sim_registry.h"
#include "../src/ui/theme.h"
#include "../validator/screen_validate.h"

/* ── Config ─────────────────────────────────────────────────────────────────── */
static constexpr int32_t SIM_W    = 320;
static constexpr int32_t SIM_H    = 480;
static constexpr int32_t SIM_ZOOM = 200;   /* 200 = 2x (base level is 100) */

static int s_cur = 0;   /* current registry index (explicit switcher) */

/* ── LVGL host tick ───────────────────────────────────────────────────────── */
static uint32_t sim_tick_cb(void) { return (uint32_t)millis(); }

/* ── Live validator dump (console) ────────────────────────────────────────── */
static const char* rule_name(sv_rule_t r) {
    switch (r) {
        case SV_OUT_OF_BOUNDS:        return "OUT_OF_BOUNDS";
        case SV_SIBLING_OVERLAP:      return "SIBLING_OVERLAP";
        case SV_ZERO_SIZE:            return "ZERO_SIZE";
        case SV_CHILD_EXCEEDS_PARENT: return "CHILD_EXCEEDS_PARENT";
        case SV_CLIPPED:              return "CLIPPED";
        default:                      return "?";
    }
}

static void validate_and_report(lv_obj_t* root, const char* id) {
    sv_report_t rep;
    screen_validate_run(root, &rep);
    if (rep.count == 0) {
        std::printf("[sim] '%s': layout clean\n", id);
        return;
    }
    std::printf("[sim] '%s': %d violation(s), %d fail(s)\n", id, rep.count, rep.fail_count);
    int stored = rep.count < SV_MAX_VIOLATIONS ? rep.count : SV_MAX_VIOLATIONS;
    for (int i = 0; i < stored; ++i) {
        const sv_violation_t* v = &rep.v[i];
        std::printf("    %-4s %-18s %s  a=(%d,%d)-(%d,%d)\n",
                    v->sev == SV_FAIL ? "FAIL" : "WARN",
                    rule_name(v->rule), v->a_desc,
                    (int)v->ra.x1, (int)v->ra.y1, (int)v->ra.x2, (int)v->ra.y2);
    }
}

/* ── Show a screen by registry index ──────────────────────────────────────── */
static void show(int idx) {
    if (idx < 0 || idx >= sim_registry_count) return;
    s_cur = idx;
    lv_obj_t* root = sim_registry[idx].create();
    if (!root) {
        std::printf("[sim] create() returned NULL for '%s'\n", sim_registry[idx].id);
        return;
    }
    lv_obj_update_layout(root);
    lv_screen_load(root);
    std::printf("[sim] showing '%s' (%d/%d)\n",
                sim_registry[idx].id, idx + 1, sim_registry_count);
    validate_and_report(root, sim_registry[idx].id);
}

/* ── Resolve --screen=<id> to a registry index ────────────────────────────── */
static int resolve_start_index(int argc, char** argv) {
    const char* want = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--screen=", 9) == 0) want = argv[i] + 9;
        else if (std::strcmp(argv[i], "--screen") == 0 && i + 1 < argc) want = argv[++i];
    }
    if (!want) return 0;
    for (int i = 0; i < sim_registry_count; ++i)
        if (std::strcmp(sim_registry[i].id, want) == 0) return i;
    std::printf("[sim] unknown --screen='%s'; starting on '%s'\n",
                want, sim_registry[0].id);
    return 0;
}

int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "");

    std::printf("[sim] ESPScreen interactive simulator\n");
    std::printf("[sim] available screens:");
    for (int i = 0; i < sim_registry_count; ++i) std::printf(" %s", sim_registry[i].id);
    std::printf("\n[sim] usage: program.exe [--screen=<id>]   (mouse = touch; tap tiles to navigate)\n");

    lv_init();
    lv_tick_set_cb(sim_tick_cb);

    /* Create the GDI window display (simulator_mode = not resizable). */
    lv_display_t* disp = lv_windows_create_display(
        L"EspScreen Sim",
        SIM_W, SIM_H,
        SIM_ZOOM,
        /*allow_dpi_override=*/false,
        /*simulator_mode=*/true);
    if (!disp) {
        std::fprintf(stderr, "[sim] lv_windows_create_display failed "
                             "(no display station / headless session?)\n");
        return 1;
    }

    /* Bind a pointer input device (mouse → LVGL touch) to this display. */
    lv_indev_t* mouse = lv_windows_acquire_pointer_indev(disp);
    if (!mouse) {
        std::fprintf(stderr, "[sim] lv_windows_acquire_pointer_indev failed\n");
        return 1;
    }

    /* REQUIRED: install the ESPScreen dark theme + custom styles (matches device;
     * without it screens render in LVGL's default light theme). */
    ui_theme::apply();

    /* Starting screen from --screen=<id> (default: first / launcher). */
    show(resolve_start_index(argc, argv));

    /* Main loop: pump LVGL timers. The GDI backend runs window message handling
     * and flushing on its own thread, so we only schedule timers/animations here.
     * claude_widget's poll task stub never runs and xQueueReceive returns empty,
     * so the network path stays inert. */
    for (;;) {
        uint32_t next = lv_timer_handler();
        if (next == LV_NO_TIMER_READY || next > 100) next = 100;
        if (next < 5) next = 5;
        Sleep(next);   /* Win32 Sleep (windows.h pulled in by the GDI driver headers) */
    }

    return 0;   /* unreachable */
}

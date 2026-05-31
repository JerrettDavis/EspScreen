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

/* ── Live-viewer (Phase 6): connect to the device fb_stream and assemble a frame.
 * Reuses the shared wire protocol; uses Winsock for TCP and the stb PNG writer
 * (this TU owns the stb impls via SIM_PNG_WRITE_IMPL) for --capture. */
#define SIM_PNG_WRITE_IMPL
#include "png_write.h"
#include "fb_proto.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <cstdint>

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

/* ── Live framebuffer viewer / capture (Phase 6) ───────────────────────────── */

static const char* arg_value(int argc, char** argv, const char* key) {
    size_t klen = std::strlen(key);
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], key, klen) == 0 && argv[i][klen] == '=')
            return argv[i] + klen + 1;
    }
    return nullptr;
}
static bool has_flag(int argc, char** argv, const char* key) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], key) == 0 ||
            std::strncmp(argv[i], key, std::strlen(key)) == 0) return true;
    return false;
}

/* Read exactly n bytes (blocking) from the socket. Returns false on EOF/error. */
static bool recv_exact(SOCKET s, uint8_t* buf, int n) {
    int got = 0;
    while (got < n) {
        int r = recv(s, (char*)buf + got, n - got, 0);
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

/* Convert the assembled RGB565-LE framebuffer to RGB888 and count non-bg pixels. */
static long fb_to_rgb888_and_count(const uint16_t* fb, std::vector<uint8_t>& out) {
    out.resize((size_t)SIM_W * SIM_H * 3);
    const uint16_t bg565 = (uint16_t)(((0x0D >> 3) << 11) | ((0x11 >> 2) << 5) | (0x17 >> 3));
    long nonbg = 0;
    for (int i = 0; i < SIM_W * SIM_H; ++i) {
        uint16_t p = fb[i];
        if (p != bg565) nonbg++;
        uint8_t r5 = (p >> 11) & 0x1F, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
        out[i*3+0] = (uint8_t)((r5 << 3) | (r5 >> 2));
        out[i*3+1] = (uint8_t)((g6 << 2) | (g6 >> 4));
        out[i*3+2] = (uint8_t)((b5 << 3) | (b5 >> 2));
    }
    return nonbg;
}

/* Headless-capturable: connect, read tiles, blit, and on the first frame-end
 * sentinel (or once the whole screen is covered) write a PNG and return.
 * If capture_path is NULL, runs an endless console-logging view loop. */
static int run_live(const char* host, int port, const char* capture_path) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "[live] WSAStartup failed\n"); return 1;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { std::fprintf(stderr, "[live] socket() failed\n"); WSACleanup(); return 1; }

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((u_short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        std::fprintf(stderr, "[live] bad host '%s'\n", host); closesocket(s); WSACleanup(); return 1;
    }
    std::printf("[live] connecting to %s:%d ...\n", host, port);
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        std::fprintf(stderr, "[live] connect failed (device offline / fb_stream not running?)\n");
        closesocket(s); WSACleanup(); return 1;
    }
    std::printf("[live] connected. assembling frame...\n");

    static uint16_t fb[SIM_W * SIM_H];
    std::memset(fb, 0, sizeof(fb));

    std::vector<uint8_t> payload;
    long tiles = 0, covered = 0;
    bool got_frame = false;
    /* Cover-tracking: a coarse mark of which 320x20 bands have been written. */
    bool band[SIM_H] = { false };

    for (;;) {
        fb_tile_hdr_t hdr;
        if (!recv_exact(s, (uint8_t*)&hdr, sizeof(hdr))) {
            std::printf("[live] socket closed by device\n"); break;
        }
        if (hdr.magic != FB_PROTO_MAGIC) {
            /* Resync: scan forward one byte at a time until the next magic. */
            continue;
        }
        if (hdr.w == 0 && hdr.h == 0) {
            std::printf("[live] frame-end sentinel after %ld tiles\n", tiles);
            got_frame = true;
            if (capture_path) break;     /* capture mode: one frame is enough */
            continue;
        }
        size_t bytes = (size_t)hdr.w * hdr.h * 2u;
        payload.resize(bytes);
        if (!recv_exact(s, payload.data(), (int)bytes)) {
            std::printf("[live] truncated tile payload — stopping\n"); break;
        }
        /* Blit the tile into the framebuffer at (x,y). */
        const uint16_t* src = (const uint16_t*)payload.data();
        for (int row = 0; row < hdr.h; ++row) {
            int dy = hdr.y + row;
            if (dy < 0 || dy >= SIM_H) continue;
            for (int col = 0; col < hdr.w; ++col) {
                int dx = hdr.x + col;
                if (dx < 0 || dx >= SIM_W) continue;
                fb[dy * SIM_W + dx] = src[row * hdr.w + col];
            }
            if (dy >= 0 && dy < SIM_H && !band[dy]) { band[dy] = true; covered++; }
        }
        tiles++;
        /* Fallback completion: if no sentinel but full height covered, finalize. */
        if (capture_path && covered >= SIM_H) { got_frame = true; break; }
    }

    closesocket(s);
    WSACleanup();

    if (capture_path) {
        std::vector<uint8_t> rgb;
        long nonbg = fb_to_rgb888_and_count(fb, rgb);
        if (!sim_write_png_rgb888(capture_path, SIM_W, SIM_H, rgb.data())) {
            std::fprintf(stderr, "[live] PNG write failed: %s\n", capture_path); return 1;
        }
        std::printf("[live] captured %dx%d (%ld tiles, %s) -> %s\n",
                    SIM_W, SIM_H, tiles, got_frame ? "frame complete" : "PARTIAL", capture_path);
        std::printf("[live] non-background pixels: %ld\n", nonbg);
        return got_frame ? 0 : 2;   /* 2 = partial frame (still wrote what we have) */
    }
    return 0;
}

int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "");

    /* ── Phase 6 live-viewer mode: --mode=live --host=<ip> [--port=N] [--capture=f]
     * Runs WITHOUT LVGL/GDI so it works in a headless session. ───────────────── */
    {
        const char* mode = arg_value(argc, argv, "--mode");
        if (mode && std::strcmp(mode, "live") == 0) {
            const char* host = arg_value(argc, argv, "--host");
            const char* ports = arg_value(argc, argv, "--port");
            const char* cap = arg_value(argc, argv, "--capture");
            if (!host) { std::fprintf(stderr, "[live] --host=<ip> required\n"); return 1; }
            int port = ports ? std::atoi(ports) : FB_PROTO_PORT;
            return run_live(host, port, cap);
        }
    }

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

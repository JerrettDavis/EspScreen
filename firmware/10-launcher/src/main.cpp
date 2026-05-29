/**
 * main.cpp — EspScreen Phase 1 entry point
 *
 * Stage 1a: LVGL 9 hello world
 * Stage 1b: adds touch::init() (NVS cal with hardcoded defaults — no auto-cal)
 * Stage 1c: adds config::mount_fs() / config::load_config() + logger
 * Stage 1b-recovery: GPIO0 + Serial factory-reset hatches before LVGL init
 *
 * Build/flash: pio run -t upload  (COM20)
 * Monitor:     pio device monitor  (115200 baud)
 */

#include <Arduino.h>
#include <lvgl.h>
#include "hal/display.h"
#include "hal/touch.h"
#include "os/logger.h"
#include "os/config.h"
#include "os/recovery.h"
#include "os/screen_router.h"
#include "os/wifi_mgr.h"
#include "os/nvs_store.h"
#include "ui/theme.h"
#include "app/builtin/launcher.h"
#include "app/builtin/claude_widget.h"
// calibrate.h intentionally excluded — cal is disabled (touch uses factory defaults)

/* ── Non-blocking serial command reader ─────────────────────────── */
static String s_serial_buf;

/* ── Stage-gate defines ─────────────────────────────────────────────
 * Comment/uncomment to enable each stage incrementally during dev.
 * Stage 1a ships with all three active; they are no-ops until their
 * respective .cpp files are fully wired up.
 */
#define STAGE_1A_DISPLAY   1
#define STAGE_1B_TOUCH     1
#define STAGE_1C_CONFIG    1

void setup() {
    Serial.begin(115200);
    delay(100);  // let serial settle enough for recovery banner

    /* ── Recovery hatches (FIRST — before LVGL, display, or FS) ─── */
    recovery::check();

    Serial.println("[main] EspScreen " ESPSCREEN_VERSION " starting...");

#if STAGE_1C_CONFIG
    /* Mount FS and load config BEFORE display init so config-driven
     * settings (backlight %, rotation) can be applied at init time.
     */
    logger_init();
    if (!config::mount_fs()) {
        Serial.println("[main E] LittleFS mount failed — using defaults");
    } else {
        config::load_config();
        LOG_I("main", "device.name=%s", config::device().name);
        LOG_I("main", "free heap pre-display=%lu", (unsigned long)esp_get_free_heap_size());
    }
#endif

#if STAGE_1A_DISPLAY
    display::init();
    ui_theme::apply();
#endif

#if STAGE_1B_TOUCH
    touch::init();  // loads NVS cal or uses Phase 0 defaults; no auto-cal
#endif

    /* ── WiFi init (non-blocking — reads NVS credentials) ───────── */
    wifi_mgr::init();

    /* ── Build and load the launcher screen ─────────────────────── */
    lv_obj_t* home = launcher::create_screen();
    Serial.printf("[main] launcher created scr=%p\n", (void*)home);
    screen_router::push(home);
    Serial.printf("[main] after router push(home): active=%p\n", (void*)lv_scr_act());
    lv_scr_load(home);  // immediate load (no animation on first screen)
    Serial.printf("[main] after lv_scr_load(home): active=%p\n", (void*)lv_scr_act());

    LOG_I("main", "UI ready. Free heap=%lu", (unsigned long)esp_get_free_heap_size());

    /* ── Serial command help banner ──────────────────────────────── */
    Serial.println("[main] Serial commands: tdbg | info | reset | wifi set/clear/status | claude set/get/poll");

    /* ── pending_cal disabled — touch uses Phase 0 factory defaults ─ */
    #if 0
    if (recovery::pending_cal()) {
        Serial.println("[main] Pending cal — entering calibrate::launch()");
        calibrate::launch();
        Serial.printf("[main] calibrate::launch() returned: active=%p\n", (void*)lv_scr_act());
    }
    #endif
}

/* ── Quote-aware argument splitter ──────────────────────────────────
 * Splits "verb arg1 arg2" respecting double-quoted tokens.
 * e.g. 'wifi set "my ssid" "pass word"' → ["wifi","set","my ssid","pass word"]
 */
static void split_args(const String& line, String* argv, int max_argc, int* out_argc) {
    *out_argc = 0;
    int i = 0;
    int n = line.length();
    while (i < n && *out_argc < max_argc) {
        /* skip whitespace */
        while (i < n && line[i] == ' ') i++;
        if (i >= n) break;
        String token = "";
        if (line[i] == '"') {
            /* quoted token */
            i++;
            while (i < n && line[i] != '"') token += line[i++];
            if (i < n) i++;  // skip closing quote
        } else {
            /* unquoted token */
            while (i < n && line[i] != ' ') token += line[i++];
        }
        argv[(*out_argc)++] = token;
    }
}

/* ── Dispatch a complete serial command line ─────────────────────── */
static void dispatch_serial_cmd(const String& cmd) {
    static const int MAX_ARGS = 6;
    String argv[MAX_ARGS];
    int argc = 0;
    split_args(cmd, argv, MAX_ARGS, &argc);
    if (argc == 0) return;

    String verb = argv[0];
    verb.toLowerCase();

    /* ── Legacy single-word commands ─────────────────────────────── */
    if (verb == "cal") {
        Serial.println("[main] 'cal' is disabled — touch uses factory defaults");

    } else if (verb == "tdbg") {
        bool next = !touch::get_debug();
        touch::set_debug(next);

    } else if (verb == "info") {
        Serial.printf("[main] Free heap: %lu  Min free heap: %lu\n",
                      (unsigned long)esp_get_free_heap_size(),
                      (unsigned long)esp_get_minimum_free_heap_size());
        Serial.printf("[main] Uptime: %lu ms\n", (unsigned long)millis());
        Serial.printf("[main] Touch debug: %s\n", touch::get_debug() ? "ON" : "OFF");

    } else if (verb == "reset") {
        Serial.println("[main] Resetting via serial command...");
        delay(100);
        ESP.restart();

    /* ── WiFi commands ────────────────────────────────────────────── */
    } else if (verb == "wifi") {
        if (argc < 2) {
            Serial.println("[wifi] Usage: wifi set <ssid> <pass> | wifi clear | wifi status");
            return;
        }
        String sub = argv[1];
        sub.toLowerCase();

        if (sub == "set") {
            if (argc < 4) {
                Serial.println("[wifi] Usage: wifi set <ssid> <pass>");
                Serial.println("[wifi] Tip:   wifi set \"ssid with spaces\" \"pass word\"");
                return;
            }
            wifi_mgr::set_credentials(argv[2].c_str(), argv[3].c_str());
            Serial.println("[wifi] Credentials saved. Reconnecting...");
            wifi_mgr::init();

        } else if (sub == "clear") {
            wifi_mgr::clear_credentials();
            Serial.println("[wifi] Credentials cleared");

        } else if (sub == "status") {
            if (wifi_mgr::is_connected()) {
                Serial.printf("[wifi] Connected  ssid=%s  rssi=%d dBm  ip=%s\n",
                              wifi_mgr::get_ssid().c_str(),
                              wifi_mgr::get_rssi(),
                              wifi_mgr::get_ip().c_str());
            } else {
                String ssid = wifi_mgr::get_ssid();
                Serial.printf("[wifi] Disconnected  ssid=%s (stored)\n",
                              ssid.isEmpty() ? "(none)" : ssid.c_str());
            }
        } else {
            Serial.printf("[wifi] Unknown subcommand: '%s'\n", sub.c_str());
        }

    /* ── Claude endpoint commands ─────────────────────────────────── */
    } else if (verb == "claude") {
        if (argc < 2) {
            Serial.println("[claude] Usage: claude set <url> | claude get | claude poll");
            return;
        }
        String sub = argv[1];
        sub.toLowerCase();

        if (sub == "set") {
            if (argc < 3) {
                Serial.println("[claude] Usage: claude set <url>");
                Serial.println("[claude] Example: claude set http://192.168.1.100:8766/status.json");
                return;
            }
            nvs_store::put_str("claude", "endpoint", argv[2].c_str());
            Serial.printf("[claude] Endpoint saved: %s\n", argv[2].c_str());

        } else if (sub == "get") {
            String url = nvs_store::get_str("claude", "endpoint", "");
            if (url.isEmpty()) {
                Serial.println("[claude] No endpoint configured");
            } else {
                Serial.printf("[claude] Endpoint: %s\n", url.c_str());
            }

        } else if (sub == "poll") {
            Serial.println("[claude] Forcing immediate poll...");
            claude_widget::poll_now();

        } else {
            Serial.printf("[claude] Unknown subcommand: '%s'\n", sub.c_str());
        }

    } else {
        Serial.printf("[main] Unknown command: '%s'\n", cmd.c_str());
        Serial.println("[main] Commands: cal | tdbg | info | reset | wifi set/clear/status | claude set/get/poll");
    }
}

/* Track active screen pointer across loop iterations to detect rogue swaps */
static lv_obj_t* s_last_active_scr = nullptr;
static int s_loop_count = 0;

void loop() {
    /* LVGL task handler — must be called regularly.
     * 5 ms delay keeps ~200 Hz polling, well above the 60 Hz refresh.
     */
    lv_timer_handler();

    /* Diagnostic: log any screen change for first 100 loop iterations */
    if (s_loop_count < 100) {
        s_loop_count++;
        lv_obj_t* cur = lv_scr_act();
        if (cur != s_last_active_scr) {
            Serial.printf("[loop #%d] screen changed: %p -> %p\n",
                          s_loop_count, (void*)s_last_active_scr, (void*)cur);
            s_last_active_scr = cur;
        }
    }

    /* ── Non-blocking serial command reader ─────────────────────── */
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            s_serial_buf.trim();
            if (s_serial_buf.length() > 0) {
                dispatch_serial_cmd(s_serial_buf);
            }
            s_serial_buf = "";
        } else {
            s_serial_buf += c;
        }
    }

    delay(5);
}

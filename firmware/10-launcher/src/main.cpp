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
#include "ui/theme.h"
#include "app/builtin/launcher.h"
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

    /* ── Build and load the launcher screen ─────────────────────── */
    lv_obj_t* home = launcher::create_screen();
    Serial.printf("[main] launcher created scr=%p\n", (void*)home);
    screen_router::push(home);
    Serial.printf("[main] after router push(home): active=%p\n", (void*)lv_scr_act());
    lv_scr_load(home);  // immediate load (no animation on first screen)
    Serial.printf("[main] after lv_scr_load(home): active=%p\n", (void*)lv_scr_act());

    LOG_I("main", "UI ready. Free heap=%lu", (unsigned long)esp_get_free_heap_size());

    /* ── Serial command help banner ──────────────────────────────── */
    Serial.println("[main] Serial commands: tdbg | info | reset  (cal disabled — factory defaults)");

    /* ── pending_cal disabled — touch uses Phase 0 factory defaults ─ */
    #if 0
    if (recovery::pending_cal()) {
        Serial.println("[main] Pending cal — entering calibrate::launch()");
        calibrate::launch();
        Serial.printf("[main] calibrate::launch() returned: active=%p\n", (void*)lv_scr_act());
    }
    #endif
}

/* ── Dispatch a complete serial command line ─────────────────────── */
static void dispatch_serial_cmd(const String& cmd) {
    if (cmd.equalsIgnoreCase("cal")) {
        Serial.println("[main] 'cal' is disabled — touch uses factory defaults");
    } else if (cmd.equalsIgnoreCase("tdbg")) {
        bool next = !touch::get_debug();
        touch::set_debug(next);
        /* feedback already printed by set_debug() */
    } else if (cmd.equalsIgnoreCase("info")) {
        Serial.printf("[main] Free heap: %lu  Min free heap: %lu\n",
                      (unsigned long)esp_get_free_heap_size(),
                      (unsigned long)esp_get_minimum_free_heap_size());
        Serial.printf("[main] Uptime: %lu ms\n", (unsigned long)millis());
        Serial.printf("[main] Touch debug: %s\n", touch::get_debug() ? "ON" : "OFF");
    } else if (cmd.equalsIgnoreCase("reset")) {
        Serial.println("[main] Resetting via serial command...");
        delay(100);
        ESP.restart();
    } else {
        Serial.printf("[main] Unknown command: '%s'\n", cmd.c_str());
        Serial.println("[main] Commands: cal | tdbg | info | reset");
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

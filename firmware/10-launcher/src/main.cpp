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
    screen_router::push(home);
    lv_scr_load(home);  // immediate load (no animation on first screen)

    LOG_I("main", "UI ready. Free heap=%lu", (unsigned long)esp_get_free_heap_size());
}

void loop() {
    /* LVGL task handler — must be called regularly.
     * 5 ms delay keeps ~200 Hz polling, well above the 60 Hz refresh.
     */
    lv_timer_handler();
    delay(5);
}

/**
 * main.cpp — EspScreen Phase 1 entry point
 *
 * Stage 1a: LVGL 9 hello world
 * Stage 1b: adds touch::init() (4-corner cal + NVS)
 * Stage 1c: adds config::mount_fs() / config::load_config() + logger
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
    delay(500);  // let serial settle
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
    touch::init();  // loads cal from NVS; runs 4-corner cal on first boot
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

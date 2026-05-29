#include "recovery.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <nvs_flash.h>

/**
 * recovery.cpp — hardware-free factory reset hatches.
 *
 * Hatch A: GPIO0 (BOOT button, active LOW, internal pullup) held low
 *          for the entire ~3 s GPIO poll window → factory reset.
 * Hatch B: Type "reset" on Serial within 5 s of boot → factory reset.
 *
 * Both hatches run before LVGL initialises, so they work even if the
 * display or touch is completely broken.
 *
 * Factory reset:
 *   1. Erase NVS partition (clears cal values, config keys, etc.)
 *   2. Mount-or-format LittleFS, then explicitly format it (wipes config.json)
 *   3. Restart
 */

#define BOOT_PIN         0          /* GPIO0 — BOOT button, active LOW */
#define GPIO_POLL_MS     3000       /* Hold BOOT for 3 s to trigger */
#define GPIO_SAMPLE_MS   100        /* Sample interval while polling */
#define SERIAL_WINDOW_MS 5000       /* Serial command window */

static void factory_reset() {
    Serial.println("[recovery] Factory reset triggered — wiping NVS and LittleFS");

    /* Erase NVS */
    nvs_flash_erase();
    Serial.println("[recovery] NVS erased");

    /* Format LittleFS */
    LittleFS.begin(true);   /* mount-or-format */
    LittleFS.format();
    LittleFS.end();
    Serial.println("[recovery] LittleFS formatted");

    delay(500);
    Serial.println("[recovery] Restarting...");
    ESP.restart();
}

namespace recovery {

void check() {
    /* ── Print recovery banner ───────────────────────────────────── */
    Serial.println("================================================");
    Serial.println("EspScreen " ESPSCREEN_VERSION " — Recovery window: 5s");
    Serial.println("Hold BOOT button OR type 'reset' to factory-reset");
    Serial.println("================================================");

    /* ── Configure GPIO0 with internal pullup ───────────────────── */
    pinMode(BOOT_PIN, INPUT_PULLUP);

    /* ── Poll both hatches concurrently for SERIAL_WINDOW_MS ────── */
    uint32_t start        = millis();
    uint32_t gpio_low_start = 0;
    bool     gpio_was_low   = false;
    String   serial_buf;

    while ((millis() - start) < SERIAL_WINDOW_MS) {
        /* ── Hatch A: GPIO0 ──────────────────────────────────────── */
        bool pin_low = (digitalRead(BOOT_PIN) == LOW);

        if (pin_low) {
            if (!gpio_was_low) {
                gpio_was_low    = true;
                gpio_low_start  = millis();
                Serial.println("[recovery] BOOT button held — keep holding for 3s to reset");
            } else if ((millis() - gpio_low_start) >= GPIO_POLL_MS) {
                Serial.println("[recovery] BOOT held 3s — triggering factory reset");
                factory_reset();
                /* Never returns */
            }
        } else {
            if (gpio_was_low) {
                Serial.println("[recovery] BOOT released — reset cancelled");
            }
            gpio_was_low   = false;
            gpio_low_start = 0;
        }

        /* ── Hatch B: Serial "reset" command ─────────────────────── */
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                serial_buf.trim();
                if (serial_buf.equalsIgnoreCase("reset")) {
                    Serial.println("[recovery] Serial 'reset' received — triggering factory reset");
                    factory_reset();
                    /* Never returns */
                }
                serial_buf = "";
            } else {
                serial_buf += c;
            }
        }

        delay(GPIO_SAMPLE_MS);
    }

    Serial.println("[recovery] Recovery window closed — booting normally");
}

} // namespace recovery

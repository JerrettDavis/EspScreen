#pragma once
// pinmap.h — EspScreen hardware pin assignments
// Board:   ESP32-WROOM-32E DevKit
// Display: LCDWiki 3.5" 320x480 IPS, ST7796 controller (TFT_eSPI Setup77-equivalent)
// Single source of truth — all firmware phases include this header.

// ── TFT (ST7796) ──────────────────────────────────────────────────────────────
#define PIN_TFT_MOSI  13
#define PIN_TFT_MISO  12
#define PIN_TFT_SCK   14
#define PIN_TFT_CS    15
#define PIN_TFT_DC     2
#define PIN_TFT_RST   -1   // Tied to EN (hardware reset); -1 = not driven by firmware
#define PIN_TFT_BL    27   // Backlight PWM; HIGH = on

// ── TOUCH (XPT2046, shared SPI bus) ──────────────────────────────────────────
#define PIN_TOUCH_CS  33
#define PIN_TOUCH_IRQ 36   // Input-only GPIO; no pull-up needed (board has it)

// ── SD CARD (dedicated VSPI bus — NOT shared with display/touch) ─────────────
// Board: Sunton ESP32-3248S035 / CYD 3.5" ST7796 variant.
// SD is wired to the ESP32 VSPI bus (GPIO 18/19/23/5), separate from
// the display/touch bus (HSPI: GPIO 14/13/12).
#define PIN_SD_CS      5
#define PIN_SD_SCK    18
#define PIN_SD_MISO   19
#define PIN_SD_MOSI   23

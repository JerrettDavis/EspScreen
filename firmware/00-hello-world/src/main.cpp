/**
 * EspScreen — Phase 0: Hello World / Bring-up
 *
 * Lights the ST7796 display via TFT_eSPI, draws a version banner, and cycles
 * through five background colours on each screen tap. Touch coords are logged
 * to Serial so you can verify the XPT2046 is alive before Phase 1 calibration.
 *
 * References:
 *   - LCDWiki 3.5" IPS product page: http://www.lcdwiki.com/3.5inch_SPI_Module_ILI9488_SKU:MSP3520
 *     (ST7796 variant uses same form-factor; consult LCDWiki datasheet for your SKU)
 *   - TFT_eSPI Setup77 (ESP32 + ST7796): see TFT_eSPI/User_Setups/Setup77_ST7796_ESP32.h
 *     (we replicate those flags via platformio.ini build_flags instead of editing the library)
 *
 * Board:   ESP32-WROOM-32E DevKit, COM20
 * Display: 320 x 480, ST7796 driver, BGR colour order
 */

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

// ── Globals ──────────────────────────────────────────────────────────────────

TFT_eSPI tft;

static const uint16_t COLORS[]     = { TFT_BLACK, TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE };
static const char*    COLOR_NAMES[] = { "BLACK",   "RED",   "GREEN",   "BLUE",   "WHITE"  };
static const uint8_t  NUM_COLORS    = sizeof(COLORS) / sizeof(COLORS[0]);

static uint8_t  colorIdx    = 0;
static uint32_t lastTouchMs = 0;
static const uint32_t DEBOUNCE_MS = 200;

// ── Helpers ───────────────────────────────────────────────────────────────────

/**
 * Fill the screen with `bg`, then draw the version banner and current colour
 * name centred on screen.  Text colour is chosen for contrast (dark bg → white
 * text, light bg → black text).
 */
void drawScreen(uint16_t bg) {
    tft.fillScreen(bg);

    // Use white text on dark backgrounds, black on light
    uint16_t textColor = (bg == TFT_WHITE) ? TFT_BLACK : TFT_WHITE;
    tft.setTextColor(textColor, bg);   // second arg = bg, suppresses ghost pixels

    // Version banner — font 4, roughly centred vertically in upper third
    tft.setTextDatum(MC_DATUM);        // middle-centre datum
    tft.setTextSize(1);

    tft.drawString("EspScreen v0.0.1", tft.width() / 2, 140, 4);  // font 4
    tft.drawString("Tap to cycle",     tft.width() / 2, 200, 2);  // font 2
    tft.drawString(COLOR_NAMES[colorIdx], tft.width() / 2, 240, 2);
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("[EspScreen v0.0.1] Phase 0 hello-world starting...");

    // Backlight on
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    // TFT init
    tft.init();
    tft.setRotation(0);   // portrait, USB connector at bottom

    // Touch calibration — placeholder values from a typical LCDWiki 3.5" unit.
    // Phase 1 will replace these with a 4-corner calibration routine stored in NVS.
    uint16_t calData[5] = { 275, 3620, 264, 3532, 1 };
    tft.setTouch(calData);

    drawScreen(COLORS[colorIdx]);

    Serial.println("Ready. Tap the screen to cycle background colors.");
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
    uint16_t x = 0, y = 0;

    if (tft.getTouch(&x, &y)) {
        uint32_t now = millis();
        if (now - lastTouchMs > DEBOUNCE_MS) {
            lastTouchMs = now;

            colorIdx = (colorIdx + 1) % NUM_COLORS;
            drawScreen(COLORS[colorIdx]);

            Serial.printf("touch x=%u y=%u  color=%s (idx=%u)\n",
                          x, y, COLOR_NAMES[colorIdx], colorIdx);
        }
    }

    delay(10);   // yield; keeps watchdog happy
}

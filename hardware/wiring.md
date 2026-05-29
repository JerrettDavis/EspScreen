# Wiring — ESP32-WROOM-32E + LCDWiki 3.5" ST7796

Pin assignments are derived from TFT_eSPI Setup77 (`User_Setups/Setup77_ST7796_ESP32.h`). They are replicated as `#define` macros in [`firmware/shared/pinmap.h`](../firmware/shared/pinmap.h) — that file is the canonical reference. This table is for quick hardware reference only.

## Pin Table

| Signal | ESP32 GPIO | Connector label | Notes |
|--------|-----------|-----------------|-------|
| TFT MOSI | 13 | SDI/MOSI | SPI data out |
| TFT MISO | 12 | SDO/MISO | SPI data in (touch read-back) |
| TFT SCK | 14 | SCK | SPI clock |
| TFT CS | 15 | LCD_CS | TFT chip-select |
| TFT DC | 2 | LCD_RS/DC | Data/Command select |
| TFT RST | — | LCD_RST | Tied to ESP32 EN (hardware reset); not driven by firmware |
| TFT BL | 27 | LCD_BL | Backlight PWM; drive HIGH to enable |
| Touch CS | 33 | T_CS | XPT2046 chip-select |
| Touch IRQ | 36 | T_IRQ | Touch interrupt; input-only GPIO; board pull-up fitted |
| SD CS | 5 | SD_CS | microSD chip-select |

## Notes

- **Shared SPI bus**: TFT, touch controller, and SD card share MOSI/MISO/SCK. Each has its own CS line.
- **Colour order is BGR**: The ST7796 on this LCDWiki module requires `TFT_RGB_ORDER=TFT_BGR`. If reds appear blue, toggle to `TFT_RGB`.
- **RST tied to EN**: The display reset line is connected to the ESP32 enable pin. A board power-cycle resets both simultaneously. Firmware sets `TFT_RST=-1` to skip the software reset pulse.
- **Backlight**: GPIO 27 is pulled HIGH in `setup()`. For dimming/idle-dim in Phase 1+, use `ledcWrite` with a PWM channel.

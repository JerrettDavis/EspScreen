# HARDWARE.md — EspScreen Hardware Reference

**Board:** LCDWiki 3.5" ESP32-32E Display
**Official product page:** https://www.lcdwiki.com/3.5inch_ESP32-32E_Display

---

## Table of Contents

1. [Board Overview](#board-overview)
2. [Pin Assignments](#pin-assignments)
3. [Display Notes](#display-notes)
4. [Touch Notes](#touch-notes)
5. [SD Card Notes](#sd-card-notes)
6. [USE_HSPI_PORT — Root Cause](#use_hspi_port--root-cause)
7. [Known Gotchas](#known-gotchas)

---

## Board Overview

| Component | Part | Notes |
|-----------|------|-------|
| MCU | ESP32-WROOM-32E | Dual-core 240 MHz, 4 MB flash, no PSRAM |
| Display | ST7796 | 320×480, IPS, 3.5" |
| Touch | XPT2046 | Resistive, shared SPI bus with display |
| Storage | microSD slot | Separate VSPI bus |
| USB-Serial | CH340 | Most boards; auto-detected by provisioning tools |

Power: 5 V via USB. Draw at full backlight is approximately 300–400 mA.

---

## Pin Assignments

Canonical source: [`firmware/shared/pinmap.h`](firmware/shared/pinmap.h)

### Display (ST7796) + Touch (XPT2046) — HSPI bus

Both the display and the touch controller share the **HSPI** peripheral
(GPIO 14/13/12). They have separate chip-select lines.

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| MOSI | 13 | Output | TFT data in; also used for touch write cycles |
| MISO | 12 | Input | Shared touch read line |
| SCK | 14 | Output | Shared clock |
| TFT CS | 15 | Output | Active low; deselects touch |
| TFT DC | 2 | Output | Data / Command select |
| TFT RST | –1 | — | Tied to EN pin; driven by the board's hardware reset circuit, not firmware |
| TFT BL | 27 | Output | Backlight PWM; **HIGH = backlight on** |
| Touch CS | 33 | Output | Active low; deselects TFT |
| Touch IRQ | 36 | Input | Input-only GPIO (no internal pull-up possible); board has a pull-up resistor fitted |

### microSD — VSPI bus (dedicated, NOT shared with display)

| Signal | GPIO | Notes |
|--------|------|-------|
| SCK | 18 | VSPI clock |
| MISO | 19 | VSPI data out |
| MOSI | 23 | VSPI data in |
| CS | 5 | Active low |

---

## Display Notes

- **Color order: BGR.** The ST7796 on this board requires the BGR color order.
  TFT_eSPI build flag: `-D TFT_RGB_ORDER=TFT_BGR`. Inverting this produces
  wrong hue on solid color fills.
- **RST = –1.** The reset line is wired to the board's EN (enable) pin and is
  controlled by the hardware reset button. Firmware does not need to drive it;
  passing –1 to TFT_eSPI skips the software reset pulse.
- **Backlight: active HIGH.** GPIO 27 drives the backlight transistor.
  Pulling it HIGH turns the backlight on; LOW turns it off. PWM dimming works
  on this pin (ledc channel).

---

## Touch Notes

- **Resistive, not capacitive.** A firm press is required; light fingertip
  contact often does not register. Use a stylus for precise input.
- **Shared bus.** The XPT2046 sits on the same HSPI bus as the ST7796 display.
  CS lines are separate. The touch IRQ (GPIO 36) is an input-only GPIO with a
  board-mounted pull-up; the firmware cannot configure an internal pull-up on
  this pin.
- **Calibration.** Raw XPT2046 ADC values do not map linearly to screen pixels.
  Phase 1 firmware includes a 4-corner calibration routine; calibration data is
  persisted to NVS.

---

## SD Card Notes

- The microSD slot is on a **dedicated VSPI bus** (GPIO 18/19/23/5), completely
  separate from the display/touch HSPI bus.
- The SD card is initialised using `SPIClass(VSPI).begin(18, 19, 23, 5)`.
- FAT32 formatted cards are supported; exFAT is not guaranteed on the Arduino
  SD library used here.
- If no SD card is present, the firmware falls back to NVS + LittleFS for
  config and credential storage.

---

## USE_HSPI_PORT — Root Cause

**The build flag `-D USE_HSPI_PORT` is mandatory for this board.**

### What it does

Without this flag, TFT_eSPI defaults to the VSPI peripheral. When the SD
initialisation call `SPIClass(VSPI).begin(18, 19, 23, 5)` runs after that, the
Arduino SPI library remaps the VSPI MISO pin from its default (GPIO 12) to the
SD MISO pin (GPIO 19). At that point GPIO 12 is no longer the VSPI MISO.

The XPT2046 sends its response on GPIO 12 (HSPI MISO, shared with TFT MISO).
After the remap every touch read comes back as `0x1FFF` (decimal 8191) —
completely saturated, the same as no device present on the bus. Touch is
permanently dead for the rest of the session.

### Why USE_HSPI_PORT fixes it

`-D USE_HSPI_PORT` forces TFT_eSPI to use the HSPI peripheral instead of VSPI.
The SD card's VSPI init can then remap VSPI MISO freely (GPIO 12 → 19) without
touching the HSPI bus that the display and touch controller use. Both buses
operate independently and correctly.

### Where to set it

In `firmware/10-launcher/platformio.ini`, under `build_flags`:

```ini
build_flags =
    ...
    -D USE_HSPI_PORT
    ...
```

Do not remove this flag. Symptoms of its absence: display works, SD works, but
**all touch reads return 8191** and the touchscreen is unresponsive.

---

## Known Gotchas

| Symptom | Cause | Fix |
|---------|-------|-----|
| Touch reads always return 8191 / touchscreen unresponsive | `USE_HSPI_PORT` missing; SD VSPI init stole GPIO 12 from XPT2046 | Add `-D USE_HSPI_PORT` to `build_flags` |
| Wrong colors / hue shift on solid fills | BGR vs RGB mismatch | Confirm `-D TFT_RGB_ORDER=TFT_BGR` in build flags |
| Screen stays black after flash | Backlight not driven | Verify GPIO 27 pulled HIGH in setup; check USB current (500 mA minimum) |
| Screen white or garbled at startup | Incorrect SPI init order | Power-cycle the board (unplug USB), reflash |
| SD card not detected | Wrong bus or card format | Confirm VSPI pins (18/19/23/5); card must be FAT32 |
| Touch IRQ never fires | GPIO 36 pull-up missing | Board pull-up is fitted; no software pull-up needed on GPIO 36 (input-only) |
| Touch calibration out of range | Calibration not run | Run the on-screen 4-corner calibration from the launcher Settings |
| Captive portal does not open automatically | Mobile OS captive-portal probe timing | Manually navigate to `http://192.168.4.1/` |
| OAuth token expiry | Tokens valid ~5 hours | Re-run `tools/provision` or let `creds-watcher` auto-refresh |

# EspScreen

Pluggable smart-display firmware for ESP32 + ST7796 IPS. EspScreen is a layered system (HAL → OS → App) that supports both standalone MicroPython/LVGL apps and a raw Slave-SPI passthrough mode for use as a USB-connected display. See [PLAN.md](PLAN.md) for the full architecture, phased milestones, and ADRs.

**Phase 0**: bring-up sketch is live — see [`firmware/00-hello-world/README.md`](firmware/00-hello-world/README.md) for build and flash instructions.

---

| | |
|---|---|
| **Board** | ESP32-WROOM-32E DevKit, flashed via COM20 |
| **Display** | LCDWiki 3.5" 320×480 IPS, ST7796 controller |
| **Touch** | XPT2046 resistive touch (SPI, shared bus) |
| **Storage** | microSD (SPI, shared bus) |
| **Driver** | TFT_eSPI (Setup77-equivalent, inline build flags) |

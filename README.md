# EspScreen

> Pluggable smart-display firmware for the LCDWiki 3.5" ESP32-32E — standalone
> LVGL launcher, on-screen WiFi setup, Anthropic Claude OAuth, live web mirror,
> and a captive-portal first-boot flow. No PC required after first flash.

![CI](https://github.com/JerrettDavis/EspScreen/actions/workflows/build.yml/badge.svg)

<!-- add screenshots of the launcher + web mirror -->

---

## Table of Contents

1. [Features](#features)
2. [Hardware](#hardware)
3. [Quick Start](#quick-start)
4. [Web Portal & Mirror](#web-portal--mirror)
5. [Tools](#tools)
6. [Project Status / Roadmap](#project-status--roadmap)
7. [License](#license)

---

## Features

- **LVGL launcher** — tile-based app grid rendered on a 320×480 IPS display
- **WiFi setup on-screen** — scan networks, enter password with the on-screen keyboard, no serial required
- **First-boot SoftAP + captive portal** — boots as `EspScreen-Setup` AP on first run; browse to `http://192.168.4.1/` to configure WiFi and Anthropic credentials with nothing but a phone
- **Web portal (port 80)** — browser UI for WiFi config, Anthropic OAuth setup, passcode, and device settings
- **Anthropic Claude OAuth** — multi-profile credential storage with automatic token refresh; set via portal or provisioning tools
- **Claude usage widget** — 5-hour and 7-day utilization gauges on the launcher
- **Live web mirror** — downscaled live screenshot at `/api/screen`; click-to-touch via `/api/touch`
- **Device passcode** — `X-EspScreen-Secret` header; set from the portal or serial `api set-secret`
- **SD card storage** — config import from microSD (VSPI bus); NVS + LittleFS fallback
- **Standalone operation** — runs fully without a paired PC or serial connection

---

## Hardware

**Board:** LCDWiki 3.5" ESP32-32E Display
([official product page](https://www.lcdwiki.com/3.5inch_ESP32-32E_Display))

- MCU: ESP32-WROOM-32E
- Display: ST7796 320×480 IPS, color order **BGR**
- Touch: XPT2046 resistive (shared HSPI bus with display)
- Storage: microSD on dedicated VSPI bus

### Pinout

**Display + Touch — HSPI bus**

| Signal | GPIO | Notes |
|--------|------|-------|
| TFT MOSI | 13 | |
| TFT MISO | 12 | Shared with T_MISO |
| TFT SCK | 14 | Shared with T_CLK |
| TFT CS | 15 | |
| TFT DC | 2 | |
| TFT RST | –1 | Tied to EN (board hardware reset) |
| TFT BL | 27 | PWM backlight, HIGH = on |
| Touch CS | 33 | XPT2046 chip-select |
| Touch IRQ | 36 | Input-only GPIO; board pull-up fitted |

**microSD — VSPI bus (separate from display)**

| Signal | GPIO |
|--------|------|
| SD SCK | 18 |
| SD MISO | 19 |
| SD MOSI | 23 |
| SD CS | 5 |

> **Critical build flag: `-D USE_HSPI_PORT`**
>
> This flag is **mandatory**. Without it, the SD card's VSPI initialisation
> (`SPIClass(VSPI).begin(18, 19, 23, 5)`) remaps the VSPI MISO from GPIO 12 to
> GPIO 19, stealing the XPT2046's data line. The result is every touch read
> returning `0x1FFF` (8191) — completely dead touch. See [HARDWARE.md](HARDWARE.md)
> for the full root-cause explanation.

See [HARDWARE.md](HARDWARE.md) for full board details, known gotchas, and
additional wiring notes.

---

## Quick Start

### Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) installed and `pio` on your PATH
- USB cable (data, not charge-only)
- Git

### 1. Clone

```sh
git clone https://github.com/JerrettDavis/EspScreen.git
cd EspScreen
```

### 2. Flash firmware

```sh
cd firmware/10-launcher

# Compile and flash the firmware
pio run -e esp32dev -t upload

# Upload the filesystem image (config, web assets)
pio run -t uploadfs
```

PlatformIO auto-detects the COM port. To specify it manually, pass
`--upload-port COM3` (Windows) or `--upload-port /dev/ttyUSB0` (Linux/Mac).

> **Note:** Do NOT run a full flash erase (`esptool --chip esp32 erase_flash`)
> before uploading unless you intend to wipe stored WiFi and OAuth credentials
> in NVS. A plain `pio run -t upload` preserves NVS.

### 3. First boot — captive portal setup

1. Power on the board. If no WiFi credentials are stored, the device boots as
   a SoftAP named **`EspScreen-Setup`**.
2. Connect your phone or laptop to that network (no password).
3. A captive portal should open automatically. If not, browse to
   **`http://192.168.4.1/`**.
4. Enter your WiFi SSID and password, then tap **Save & Connect**.
5. On the next screen, enter your Anthropic OAuth credentials (or skip and
   provision later via the tools).

The device reboots, joins your network, and the launcher appears.

### 4. Serial monitor

```sh
pio device monitor
```

Baud rate is 115200. Press `Ctrl+C` to exit.

---

## Web Portal & Mirror

Once the device is on your network, open **`http://<device-ip>/`** in a browser
(the IP is printed to serial at boot as `[net] IP: ...`).

| Tab | Description |
|-----|-------------|
| **WiFi** | Scan and connect to networks |
| **Anthropic** | Add / switch Claude OAuth profiles |
| **Mirror** | Live downscaled display preview; click to send touch events |
| **Settings** | Rename device, set passcode, reboot |

The **Machine API** runs on port **8080** (`/api/*`). The **web portal** runs on
port **80**.

### Passcode

Set a passcode from the Settings tab or via serial:

```
api set-secret <your-secret>
```

Once set, all API requests must include the header `X-EspScreen-Secret: <your-secret>`.

### Mirror commands

Toggle the live mirror from the serial console:

```
mirror on
mirror off
```

---

## Tools

Host-side Node.js utilities (Node ≥ 18, no global installs required):

| Tool | Purpose | README |
|------|---------|--------|
| `tools/provision` | Push Claude OAuth tokens + WiFi to the board over USB serial | [provision/README.md](tools/provision/README.md) |
| `tools/creds-watcher` | Watch `~/.claude/.credentials.json` and auto-push refreshed tokens | [creds-watcher/README.md](tools/creds-watcher/README.md) |
| `tools/claude-endpoint` | Local HTTP server exposing Claude usage JSON for the on-board widget | [claude-endpoint/README.md](tools/claude-endpoint/README.md) |

```sh
cd tools/provision && npm install
node provision.js          # auto-detect port, push tokens
node provision.js --help   # all options
```

---

## Project Status / Roadmap

**Current release: v0.1.0 — Phase 1 complete.**

| Phase | Goal | Status |
|-------|------|--------|
| 0 — Bring-up | TFT_eSPI hello-world, touch sanity | Done |
| **1 — UI base** | **LVGL launcher, WiFi UI + portal, Claude OAuth, SD, web mirror** | **Done — v0.1.0** |
| 2 — MicroPython port | lv_micropython build, LittleFS app loader | Planned |
| 3 — Real apps | Claude widget polish, calculator | Planned |
| 4 — Slave-SPI mode | ESP-IDF spi_slave + DMA pipeline, host streaming | Planned |
| 5 — Updates & trust | OTA, ed25519-signed app manifests | Planned |

See [PLAN.md](PLAN.md) for architecture, ADRs, and detailed milestone specs.

---

## License

[MIT](LICENSE) — Copyright 2026 Jerrett Davis

# PLAN.md — EspScreen Firmware Architecture

**Project**: EspScreen — pluggable smart-display firmware for ESP32 + ST7796 IPS
**Target**: ESP32-WROOM-32E DevKit on COM20, LCDWiki 3.5" 320x480 IPS + XPT2046 + microSD
**Status**: Phase 1 complete — v0.1.0 released
**Owner**: JerrettDavis
**Last revised**: 2026-05-28

---

## 1. Architecture Overview

EspScreen is a four-layer system. Each layer has one job and a stable interface to the layer above.

```
+---------------------------------------------------------------+
|                       App Layer                               |
|   Launcher UI | Claude Widget | Calculator | User apps (.py)  |
|   - Loaded from /apps in LittleFS at boot                     |
|   - Pure MicroPython, LVGL bindings, no hardware knowledge    |
+---------------------------------------------------------------+
|                       OS Layer                                |
|   App loader | Config manager | WiFi/HTTP | LVGL event loop   |
|   Mode switcher (Standalone <-> Slave-SPI) | Logger | LED FX  |
|   - MicroPython + lv_micropython (Phase 1+)                   |
|   - C "frozen modules" expose HAL to Python                   |
+---------------------------------------------------------------+
|                       HAL Layer (C)                           |
|   TFT driver (ST7796, DMA) | Touch driver (XPT2046)           |
|   SD driver | NVS/LittleFS | Pin map | Backlight PWM          |
|   - Single source of truth for pins (shared/pinmap.h)         |
+---------------------------------------------------------------+
|                  Slave-SPI Mode (parallel branch)             |
|   ESP-IDF spi_slave_* task pinned to Core 0                   |
|   DMA blit to ST7796 on Core 1 | shared framebuffer in PSRAM  |
|   - Activated by mode flag; bypasses OS/App layers            |
+---------------------------------------------------------------+
                              |
                       Hardware: ESP32-WROOM-32E
```

**Flow in Standalone mode**: boot -> HAL init -> read `/config.json` -> mount LittleFS -> start LVGL -> launcher scans `/apps` -> user taps tile -> app's `main.py` runs in MicroPython VM with LVGL handle.

**Flow in Slave-SPI mode**: boot -> HAL init -> detect mode pin (or NVS flag) -> skip MicroPython -> start `spi_slave` task on Core 0 -> Core 1 owns the display, blits incoming frames via DMA. Host machine speaks a small framed protocol (header: cmd, x, y, w, h, len; payload: raw RGB565).

---

## 2. Folder Structure

```
EspScreen/
├── firmware/
│   ├── 00-hello-world/         # Phase 0: PlatformIO Arduino sketch, TFT_eSPI bring-up
│   ├── 10-launcher/            # Phase 1+: MicroPython base + lv_micropython build
│   ├── 20-slave-spi/           # Phase 4: standalone ESP-IDF project for slave mode
│   └── shared/                 # cross-firmware headers (pinmap.h, version.h)
├── apps/                       # uploadable MicroPython apps (Phase 3+)
├── tools/                      # flash scripts, app uploaders, FS image builders
├── docs/                       # architecture, app API, slave protocol, ADRs
├── hardware/                   # wiring, photos, datasheets
├── PLAN.md
├── README.md
└── .gitignore
```

---

## 3. Phased Milestones

| Phase | Goal | Exit criteria |
|-------|------|---------------|
| **0 — Bring-up** | TFT_eSPI hello-world + touch sanity | Screen lights up, fills R/G/B on tap, Serial prints touch x/y |
| **1 — UI base** | LVGL on Arduino, JSON config from LittleFS, WiFi captive portal | Launcher grid renders, joins WiFi, persists SSID |
| **2 — MicroPython port** | lv_micropython build for this board, LittleFS app loader | `mpremote` drops a `.py` into `/apps`, launcher discovers it |
| **3 — Real apps** | Claude widget (HTTP+JSON+render), calculator | Widget polls 60s, 3 gauges; calc handles 4 ops |
| **4 — Slave-SPI mode** | ESP-IDF spi_slave task + DMA pipeline, mode switcher | Host streams 320x480 RGB565 @ >=15 fps |
| **5 — Updates & trust** | OTA for base, ed25519-signed app manifests | Reject unsigned apps; rollback on boot loop |

Do not start phase N+1 until phase N's exit criteria are met and tagged.

---

## 4. Architectural Decision Records

- **ADR-001 Framework**: Phase 0 = Arduino + PlatformIO + TFT_eSPI (fastest path to lit pixels). Phase 1+ = MicroPython + lv_micropython. Escape hatch: any phase may fall back to pure ESP-IDF.
- **ADR-002 App loader**: Apps live in `/apps/<name>/` on LittleFS. Manifest declares entry point, name, icon, polling, permissions. Python-only — no dynamic native code.
- **ADR-003 Config storage**: `config.json` in LittleFS root (human-editable). NVS reserved for WiFi creds, mode flag, OTA rollback counter.
- **ADR-004 Networking**: WiFi only for v1. BLE deferred but reserved in config schema.
- **ADR-005 Updates**: Base firmware = dual OTA partitions with ESP-IDF rollback. Apps = overwrite `/apps/<name>/` over HTTP, no reboot needed. Phase 5 adds ed25519 signing.

---

## 5. Phase 0 Deliverable Spec

See `firmware/00-hello-world/`. Acceptance: screen lit, "EspScreen v0.0.1" text legible, taps cycle through R/G/B/W backgrounds, Serial logs sensible touch x/y in 0–320 / 0–480 range.

---

## 6. Config System Sketch (`config.json` in LittleFS root, Phase 1+)

```json
{
  "version": 1,
  "device": { "name": "EspScreen-01", "mode": "standalone" },
  "display": { "rotation": 0, "backlight_pct": 80, "idle_dim_pct": 20, "idle_timeout_sec": 60 },
  "pins": {
    "tft":   { "mosi": 13, "miso": 12, "sclk": 14, "cs": 15, "dc": 2, "rst": -1, "bl": 27 },
    "touch": { "cs": 33, "irq": 36 },
    "sd":    { "cs": 5 },
    "leds":  []
  },
  "network": {
    "wifi": { "ssid": "", "password": "", "hostname": "espscreen" },
    "ble":  { "enabled": false }
  },
  "apps": {
    "autostart": "launcher",
    "claude_widget": { "endpoint": "http://YOUR-PC-IP:8766/status.json", "poll_sec": 60, "timeout_sec": 5 }
  },
  "leds": { "default_effect": "off", "alert_effect": "pulse_red" },
  "security": { "allow_unsigned_apps": true, "ota_url": "" },
  "logging": { "level": "info", "to_sd": false }
}
```

Loader contract: missing keys fall back to compiled-in defaults; unknown keys preserved and logged.

---

## 7. Risks and Open Questions

1. **lv_micropython image size** — may exceed default 1.2 MB app partition. Custom `partitions.csv` needed before Phase 2.
2. **PSRAM** — ESP32-WROOM-32E has no PSRAM. Full 320x480 RGB565 framebuffer is 307 KB; slave-SPI mode must use tile blits or move to WROVER. **Decide before Phase 4.**
3. **Touch calibration** — Phase 0 uses placeholders. Phase 1 must add a 4-corner cal routine, persist to NVS.
4. **Slave-SPI wire protocol** — needs `docs/slave_protocol.md` before Phase 4. Decide framing, backpressure, max fps.
5. **App sandboxing** — single MicroPython VM. Decide cooperative vs hard isolation before Phase 3.
6. **App signing key custody** — Phase 5 needs ed25519 keypair location and process.
7. **Claude endpoint hosting** — out of scope for firmware, but widget UX depends on it.
8. **Power** — battery operation out of scope for v1; config reserves keys for future.

---

**Next action**: implement Phase 0 per `firmware/00-hello-world/`, flash COM20, verify acceptance, tag `v0.0.1`.

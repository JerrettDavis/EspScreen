# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

---

## [0.1.0] — 2026-05-28

### Added

- **LVGL launcher** — tile-based app grid on the 320×480 IPS display
- **On-screen WiFi configuration** — SSID scan, on-screen keyboard, and
  network join without a PC
- **First-boot SoftAP + captive portal** — device boots as `EspScreen-Setup`
  AP on first run; browse to `http://192.168.4.1/` to configure WiFi and
  Anthropic credentials with only a phone
- **Web portal (port 80)** — browser UI for WiFi, Anthropic OAuth, passcode,
  and device settings
- **Anthropic Claude OAuth** — multi-profile credential storage with automatic
  token refresh; configurable via web portal or host provisioning tools
- **Claude usage widget** — 5-hour and 7-day utilization gauges on the launcher
- **Live web mirror** — downscaled live screenshot at `/api/screen`;
  click-to-touch injection via `/api/touch`
- **Device passcode** — `X-EspScreen-Secret` header model; set via portal or
  `api set-secret` serial command
- **SD card storage** — config import from microSD (dedicated VSPI bus:
  SCK=18/MISO=19/MOSI=23/CS=5)
- **NVS + LittleFS fallback** — WiFi credentials and OAuth tokens persist
  without an SD card
- **Serial commands** — `net status`, `mirror on/off`, `tdbg`, `cal`,
  `api set-secret`, `reboot`
- **Host tools** — `tools/provision` (USB serial OAuth push),
  `tools/creds-watcher` (auto-refresh daemon), `tools/claude-endpoint`
  (local usage JSON server for the on-board widget)

### Fixed

- `USE_HSPI_PORT` build flag added — prevents SD VSPI initialisation from
  remapping GPIO 12 (XPT2046 MISO) to GPIO 19, which caused all touch reads
  to return `0x1FFF` (8191) and rendered the touchscreen completely dead

### Phase 0 (v0.0.1 — prior)

- TFT_eSPI bring-up sketch (`firmware/00-hello-world`): display lights up,
  shows "EspScreen v0.0.1", cycles backgrounds on tap, serial prints touch
  coordinates

---

[Unreleased]: https://github.com/JerrettDavis/EspScreen/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/JerrettDavis/EspScreen/releases/tag/v0.1.0

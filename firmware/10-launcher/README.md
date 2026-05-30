# firmware/10-launcher — Phase 1 Launcher Firmware

The main EspScreen firmware: LVGL launcher UI, WiFi configuration, Anthropic
Claude OAuth, SD card storage, live web mirror, and a captive-portal first-boot
flow. Runs standalone — no paired PC or serial connection required after initial
setup.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Build & Flash](#build--flash)
3. [First-Boot Setup](#first-boot-setup)
4. [Web Portal](#web-portal)
5. [Provisioning Claude Tokens](#provisioning-claude-tokens)
6. [Serial Commands](#serial-commands)
7. [Troubleshooting](#troubleshooting)

---

## Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)
  — install and ensure `pio` is on your PATH
- USB cable (data-capable, not charge-only)
- ESP32 board: LCDWiki 3.5" ESP32-32E Display

---

## Build & Flash

All commands run from the `firmware/10-launcher/` directory.

```sh
cd firmware/10-launcher

# 1. Compile and upload firmware
pio run -e esp32dev -t upload

# 2. Upload filesystem image (web portal assets, default config)
pio run -t uploadfs
```

PlatformIO auto-detects the COM port. To specify it manually:

```sh
pio run -e esp32dev -t upload --upload-port COM3          # Windows
pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0  # Linux / Mac
```

> **Important:** Do NOT run `esptool --chip esp32 erase_flash` before uploading
> unless you intentionally want to wipe stored WiFi and OAuth credentials. A
> plain `pio run -t upload` preserves NVS (WiFi credentials, OAuth tokens, and
> device settings).

### Serial monitor

```sh
pio device monitor
```

Baud rate: 115200. Press `Ctrl+C` to exit.

---

## First-Boot Setup

1. Flash the firmware and filesystem (both steps above).
2. Power on. The device boots as a SoftAP named **`EspScreen-Setup`** if no
   WiFi credentials are stored.
3. Connect your phone or laptop to `EspScreen-Setup` (no password).
4. A captive portal opens automatically. If it does not, navigate to
   **`http://192.168.4.1/`**.
5. Enter your WiFi SSID and password on the WiFi screen, then tap
   **Save & Connect**.
6. On the Anthropic screen, enter your Claude OAuth credentials (or skip and
   provision them later with the host tools).
7. The device reboots, joins your network, and displays the launcher.

The board's IP address is printed to serial at boot:

```
[net] IP: 192.168.1.42
```

---

## Web Portal

Open **`http://<device-ip>/`** in a browser (port 80).

| Tab | Function |
|-----|----------|
| WiFi | Scan networks, connect, view current status |
| Anthropic | Add and switch Claude OAuth profiles |
| Mirror | Live downscaled display preview; click to inject touch events |
| Settings | Rename device, set/clear passcode, reboot |

The machine API is on port **8080** (`/api/*`). The human portal is on port
**80**.

---

## Provisioning Claude Tokens

### Via web portal

Open the **Anthropic** tab and paste your access token and refresh token.

### Via tools/provision (USB serial)

From the repo root:

```sh
cd tools/provision
npm install
node provision.js                        # auto-detect port, push tokens
node provision.js --port COM3            # specify port
node provision.js --wifi-ssid "Net" --wifi-pass "pass"  # also set WiFi
```

### Via tools/creds-watcher (auto-refresh daemon)

```sh
cd tools/creds-watcher
npm install
npm start   # watches ~/.claude/.credentials.json and pushes on change
```

See [tools/README.md](../../tools/README.md) for the full tools reference.

---

## Serial Commands

Connect with `pio device monitor` (115200 baud) and type commands at the prompt.

| Command | Description |
|---------|-------------|
| `net status` | Print IP address, WiFi SSID, signal strength |
| `api set-secret <secret>` | Set the device passcode (X-EspScreen-Secret header) |
| `mirror on` | Enable the live web mirror at `/api/screen` |
| `mirror off` | Disable the live web mirror |
| `tdbg` | Toggle touch debug mode — prints raw XPT2046 ADC values on each tap |
| `cal` | Start the 4-corner touch calibration routine |
| `reboot` | Software reset the device |

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Touchscreen completely unresponsive; all taps ignored | `USE_HSPI_PORT` build flag missing — SD VSPI init stole GPIO 12 | Confirm `-D USE_HSPI_PORT` is in `platformio.ini` build_flags. See [HARDWARE.md](../../HARDWARE.md) for full explanation |
| Touch reads return 8191 (`0x1FFF`) in `tdbg` mode | Same as above | Same fix |
| SD card not detected / mount fails | Wrong bus, unformatted card, or missing CS | Card must be FAT32; CS is GPIO 5; bus is VSPI (18/19/23/5) |
| Captive portal does not open automatically | Mobile OS probe timing | Navigate manually to `http://192.168.4.1/` |
| Colors wrong / hue-shifted | BGR vs RGB mismatch | Confirm `-D TFT_RGB_ORDER=TFT_BGR` in build_flags |
| Screen stays black after flash | Backlight not driven | GPIO 27 must go HIGH; check USB power (500 mA minimum) |
| OAuth token expired — widget shows auth error | Claude tokens valid ~5 hours | Re-run `tools/provision` or start `creds-watcher`; portal Anthropic tab also works |
| `pio run -t upload` fails — "No serial port" | Wrong or missing COM port | Check `pio device list`; pass `--upload-port` explicitly |
| `pio run -t upload` fails — "Permission denied" | Port held by another program | Close Serial Monitor, VS Code serial terminal, or other COM port users |
| Web portal unreachable | Device not on network, or wrong IP | Check serial for `[net] IP:` line; verify WiFi credentials via captive portal |

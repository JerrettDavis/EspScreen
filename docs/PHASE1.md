# PHASE1.md — EspScreen Phase 1: LVGL UI Base

**Status**: Design complete
**Prerequisites**: Phase 0 verified, COM20, TFT_eSPI + XPT2046 working
**Target tag**: `v0.1.0`

## 1. Summary

Phase 1 promotes the bare-metal TFT_eSPI sketch into an LVGL-powered UI shell. HAL gets an LVGL 9 display driver bridge and calibrated touch input. OS layer adds LittleFS + `config.json`, NVS storage, WiFi credentials + captive portal, NTP. App layer gets a launcher grid (5 builtin tiles), settings, about, touch-test, and a status bar. Delivered in 6 staged sub-stages 1a–1f.

## 2. Decisions

| # | Question | Decision | Rationale |
|---|----------|----------|-----------|
| 1 | LVGL version | **LVGL 9.2.2** | Current stable; `lv_tft_espi_create()` API eliminates custom flush_cb. LVGL 8 is maintenance-only. |
| 2 | Display bridge | **Keep TFT_eSPI** (Phase 0 carried forward) | LVGL 9 ships first-class `lv_tft_espi` driver. No PSRAM here so LovyanGFX gains nothing. |
| 3 | Partitions | **Custom 4 MB layout: dual OTA + 768 KB LittleFS** | Lands dual OTA early for Phase 5 OTA rollback. |
| 4 | Captive portal | **Hand-rolled `WebServer` + `DNSServer`** (~200 lines) | tzapu/WiFiManager has crash on arduino-esp32 ≥ 3.1.0 (issue #1797). Zero new deps. |
| 5 | Config storage | **ArduinoJson 7 + LittleFS `config.json`** | File missing → copy defaults. Parse error → use compiled defaults, keep file for debug. Unknown keys preserved. |
| 6 | NVS namespaces | `wifi`, `touch`, `system` | Credentials + hardware state. Schema in §6. |
| 7 | Touch cal UX | **4-corner sequential tap**, raw ADC → affine map, NVS-persisted | Auto-trigger on first boot if `touch.calibrated == 0`. |
| 8 | Launcher model | **`kBuiltinApps[]` compile-time array** of `AppEntry` | Phase 2/3 extends with FS-scanned dynamic entries. |
| 9 | Screen routing | **`ScreenRouter` singleton** + `lv_scr_load_anim` + on-screen back button | No physical buttons on board. |

## 3. Folder Structure

```
firmware/10-launcher/
├── platformio.ini
├── partitions.csv
├── lv_conf.h
├── data/config.default.json
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── app_registry.h / .cpp
│   │   └── builtin/{launcher,settings,touch_test,about,claude_stub,calculator_stub}.h/.cpp
│   ├── hal/{display,touch,backlight,leds}.h/.cpp
│   ├── os/{config,nvs_store,wifi_mgr,time_sync,logger,screen_router}.h/.cpp
│   └── ui/{theme,statusbar,widgets}.h/.cpp
└── README.md
```

## 4. Build Config

**IMPORTANT**: Use `lv_conf.h` for ALL LVGL config. Do NOT also pass LVGL settings as `-D` flags (would cause double-definition or shell-escaping issues with pointer expressions like `LV_FONT_DEFAULT`). Only `LV_CONF_INCLUDE_SIMPLE` belongs in build_flags. Keep TFT_eSPI `-D` flags from Phase 0.

### `platformio.ini` (final, cleaned)
```ini
[env:esp32dev]
platform  = espressif32
board     = esp32dev
framework = arduino
upload_port   = COM20
monitor_port  = COM20
monitor_speed = 115200

board_build.partitions = partitions.csv
board_build.filesystem = littlefs

lib_deps =
  bodmer/TFT_eSPI@^2.5.43
  lvgl/lvgl@^9.2.2
  bblanchon/ArduinoJson@^7.2.0

build_flags =
  ; ── TFT_eSPI (identical to Phase 0) ───
  -D USER_SETUP_LOADED=1
  -D ST7796_DRIVER=1
  -D TFT_WIDTH=320
  -D TFT_HEIGHT=480
  -D TFT_MISO=12
  -D TFT_MOSI=13
  -D TFT_SCLK=14
  -D TFT_CS=15
  -D TFT_DC=2
  -D TFT_RST=-1
  -D TFT_BL=27
  -D TFT_BACKLIGHT_ON=HIGH
  -D TOUCH_CS=33
  -D LOAD_GLCD=1
  -D LOAD_FONT2=1
  -D LOAD_FONT4=1
  -D SPI_FREQUENCY=40000000
  -D SPI_TOUCH_FREQUENCY=2500000
  -D TFT_RGB_ORDER=TFT_BGR
  ; ── LVGL: only the include flag here; everything else in lv_conf.h ───
  -D LV_CONF_INCLUDE_SIMPLE
  ; ── Project ───
  -D ESPSCREEN_VERSION='"0.1.0"'
  -D ESPSCREEN_PHASE=1
  -I "${PROJECT_DIR}"
```

### `partitions.csv`
```
# Name,     Type, SubType,  Offset,    Size,    Flags
nvs,         data, nvs,      0x9000,    0x5000,
otadata,     data, ota,      0xe000,    0x2000,
app0,        app,  ota_0,    0x10000,   0x180000,
app1,        app,  ota_1,    0x190000,  0x180000,
spiffs,      data, spiffs,   0x310000,  0xC0000,
coredump,    data, coredump, 0x3D0000,  0x20000,
```

### `lv_conf.h` (project root: `firmware/10-launcher/lv_conf.h`)
```c
#pragma once
#define LV_CONF_H

#define LV_COLOR_DEPTH          16
#define LV_HOR_RES_MAX          320
#define LV_VER_RES_MAX          480
#define LV_MEM_SIZE             (48U * 1024U)

#define LV_TICK_CUSTOM          1
#define LV_TICK_CUSTOM_INCLUDE  <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR  (millis())

#define LV_INDEV_DEF_READ_PERIOD  30
#define LV_USE_TFT_ESPI         1

#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1

#define LV_USE_THEME_DEFAULT    1
#define LV_USE_THEME_MONO       0
#define LV_USE_THEME_SIMPLE     0

#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

#define LV_USE_BTN              1
#define LV_USE_LABEL            1
#define LV_USE_IMG              1
#define LV_USE_GRID             1
#define LV_USE_FLEX             1
#define LV_USE_MSGBOX           1
#define LV_USE_TEXTAREA         1
#define LV_USE_KEYBOARD         1
#define LV_USE_ARC              0
#define LV_USE_CHART            0
#define LV_USE_TABLE            0
#define LV_USE_CALENDAR         0
#define LV_USE_SPINBOX          0
#define LV_USE_SPAN             0
#define LV_USE_METER            0
```

(If LVGL 9 still requires `LV_COLOR_16_SWAP` or the renderer config, check the lvgl release notes and add only the minimum needed for a clean build.)

## 5. Implementation Order

### Stage 1a — LVGL hello world
- Create folder tree (§3)
- Copy platformio.ini, partitions.csv, lv_conf.h (§4)
- `hal/display.cpp`: `lv_init()`, `lv_tft_espi_create(320, 480, draw_buf, sizeof(draw_buf))` where `static lv_color_t draw_buf[320 * 20]`
- `main.cpp`: `setup()` calls `display::init()`, creates `lv_label` "Hello LVGL 9". `loop()` runs `lv_timer_handler(); delay(5);`
- Acceptance: "Hello LVGL 9" rendered, no LVGL asserts on Serial
- Commit: `feat(10-1a): LVGL 9.2.2 hello world, project skeleton, partitions`

### Stage 1b — Touch + 4-corner cal + NVS
- `os/nvs_store.cpp`: namespace-scoped open/close, get/put for i32/str/u8
- `hal/touch.cpp`:
  - Register `lv_indev_t` with `LV_INDEV_TYPE_POINTER`
  - `read_cb`: `tft.getTouch(&raw_x, &raw_y)`, apply affine map from NVS, emit PRESSED/RELEASED
  - `run_calibration()`: full-screen black overlay; render white crosshair at each corner sequentially; 5 raw samples per corner, discard min+max, average 3 survivors; compute linear X and Y maps; write to NVS; set `calibrated = 1`
  - `load_calibration()`: read NVS; if `calibrated == 0` → run_calibration()
- Add a label on the Stage 1a screen showing mapped X/Y as you touch; a dot moves to the tapped position
- Acceptance: after cal, dot lands ±8 px of tap at all corners. NVS survives power-cycle. **NOTE**: implementer cannot complete the 4-corner cal — user must physically tap the corners. Report this for user to verify.
- Commit: `feat(10-1b): touch indev + 4-corner calibration + NVS persistence`

### Stage 1c — LittleFS + config.json
- `data/config.default.json`: full schema from `PLAN.md` §6, all fields populated with sensible defaults
- `os/config.cpp`:
  - `mount_fs()`: `LittleFS.begin(true)`
  - `load_config()`: if `/config.json` absent → copy `/config.default.json` to `/config.json`; parse with `JsonDocument`; merge with compiled defaults; parse error → log + use compiled defaults
  - `merge_with_defaults(doc)`: for every expected key, if null write default into doc and re-save; unknown keys untouched
  - Typed accessors: `display()`, `network()`, `device()`, `apps()`
- `os/logger.cpp`: LOG_I/W/E macros; `[TAG LEVEL ts_ms] msg` to Serial; 20-line ring buffer
- `main.cpp`: `config::mount_fs(); config::load_config();` before display init; log `device.name`
- Upload filesystem: `pio run -t uploadfs`
- Acceptance: Serial prints `Loaded: device.name=EspScreen-01`. Change name in default → uploadfs → reboot → new name in serial.
- Commit: `feat(10-1c): LittleFS mount + ArduinoJson config loader with defaults merge`

## 6. NVS Schema

| Namespace | Key | Type | Default | Notes |
|-----------|-----|------|---------|-------|
| `wifi` | `ssid` | string | "" | Set by captive portal (1d) |
| `wifi` | `pass` | string | "" | Set by captive portal (1d) |
| `touch` | `cal_x_min` | i32 | 275 | Raw XPT2046 left edge |
| `touch` | `cal_x_max` | i32 | 3620 | Raw XPT2046 right edge |
| `touch` | `cal_y_min` | i32 | 264 | Raw XPT2046 top edge |
| `touch` | `cal_y_max` | i32 | 3532 | Raw XPT2046 bottom edge |
| `touch` | `calibrated` | u8 | 0 | Set to 1 after first successful cal |
| `system` | `mode_flag` | u8 | 0 | 0=standalone, 1=slave (Phase 4) |
| `system` | `ota_rb_count` | i32 | 0 | OTA rollback counter (Phase 5) |
| `system` | `fact_reset` | u8 | 0 | Reset sentinel |

NVS key strings must be ≤ 15 chars.

## 7. LittleFS Layout

```
/
└── config.default.json
```

Firmware copies to `/config.json` on first boot. Upload: `pio run -t uploadfs`.

## 8. Risks

- **Heap exhaustion** with LVGL (48 KB) + WiFi (~80 KB) + ArduinoJson + draw buffer. Profile `esp_get_free_heap_size()`; if < 60 KB free, reduce `LV_MEM_SIZE` to 32 KB.
- **lv_conf.h not found** → silent fallback to template, black screen. Verify `-I "${PROJECT_DIR}"` is set.
- **Binary > 1.5 MB** → strip more widgets.
- **Build time** first clean ~4-6 min.

## 10. Acceptance (Phase 1 full)

- Launcher loads in 5 s
- All 5 tiles, no clipping, status bar above
- Tap → screen loads, back → launcher; 10 round-trips clean
- Touch ±8 px at corners; cal persists
- First boot → AP "EspScreen-Setup"; submit on phone → connects
- Auto-connects after power cycle
- Status bar: WiFi, time (NTP), uptime
- Config round-trip works (edit default → uploadfs → reboot → new value)
- Settings: recalibrate, factory reset
- About: version, free heap, log lines
- Serial clean (no assert/panic/Guru)
- Binary ≤ 1.4 MB

---

(Full Phase 1 reference. Stages 1d-1f are described at higher level — implementation deferred to next run.)

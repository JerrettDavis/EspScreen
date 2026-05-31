/**
 * main.cpp — EspScreen Phase 1 entry point
 *
 * Stage 1a: LVGL 9 hello world
 * Stage 1b: adds touch::init() (NVS cal with hardcoded defaults — no auto-cal)
 * Stage 1c: adds config::mount_fs() / config::load_config() + logger
 * Stage 1b-recovery: GPIO0 + Serial factory-reset hatches before LVGL init
 *
 * Build/flash: pio run -t upload  (COM20)
 * Monitor:     pio device monitor  (115200 baud)
 *
 * Serial commands:
 *   tdbg | info | reset
 *   wifi add <ssid> <pass>
 *   wifi remove <ssid>
 *   wifi list
 *   wifi prefer <ssid>
 *   wifi clear
 *   wifi status
 *   claude profile add <label>
 *   claude profile remove <label>
 *   claude profile list
 *   claude profile use <label>
 *   claude token set <label> <access> <refresh> <expires_unix_sec>
 *   claude poll
 *   claude get
 *   claude set <url>  (deprecated no-op)
 *   mirror on
 *   mirror off
 *   mirror status
 */

#include <Arduino.h>
#include <lvgl.h>
#include "hal/display.h"
#include "hal/touch.h"
#include "os/logger.h"
#include "os/config.h"
#include "os/recovery.h"
#include "os/screen_router.h"
#include "os/wifi_profiles.h"
#include "os/claude_auth.h"
#include "os/nvs_store.h"
#include "os/api_server.h"
#include "os/net_manager.h"
#include "os/web_portal.h"
#include "os/screen_mirror.h"
#include "os/sd_store.h"
#include "ui/theme.h"
#include "app/builtin/launcher.h"
#include "app/builtin/claude_widget.h"
#ifdef FB_STREAM
#include "os/fb_stream.h"
#endif

/* ── Non-blocking serial command reader ─────────────────────────── */
static String s_serial_buf;

/* ── Stage-gate defines ─────────────────────────────────────────────
 * Comment/uncomment to enable each stage incrementally during dev.
 */
#define STAGE_1A_DISPLAY   1
#define STAGE_1B_TOUCH     1
#define STAGE_1C_CONFIG    1

void setup() {
    Serial.begin(115200);
    delay(100);

    /* ── Recovery hatches (FIRST) ────────────────────────────────── */
    recovery::check();

    Serial.println("[main] EspScreen " ESPSCREEN_VERSION " starting...");

#if STAGE_1C_CONFIG
    logger_init();
    if (!config::mount_fs()) {
        Serial.println("[main E] LittleFS mount failed — using defaults");
    } else {
        config::load_config();
        LOG_I("main", "device.name=%s", config::device().name);
        LOG_I("main", "free heap pre-display=%lu", (unsigned long)esp_get_free_heap_size());
    }
#endif

#if STAGE_1A_DISPLAY
    display::init();
    ui_theme::apply();
#endif

    /* ── Screen mirror init (after display/active-screen is ready) ──
     * screen_mirror::init() allocates the shadow buffer.
     * screen_mirror::enable() calls LVGL functions that require the
     * active screen to be valid. Must run AFTER display::init(). */
    screen_mirror::init();   // heap-alloc shadow buffer before first enable()
    screen_mirror::enable(config::mirror().enabled);

#if STAGE_1B_TOUCH
    touch::init();
#endif

    /* ── SD card init (after display/touch; graceful if absent) ─────
     * Required integration point for the sd_store parallel worker.    */
    sd_store::init();

    /* ── Import config overrides from SD if a config file is present ─
     * config::import_from_sd_if_present() is added by the SD worker. */
    config::import_from_sd_if_present();

    /* ── WiFi + portal state machine ────────────────────────────────
     * net_manager::init() calls wifi_profiles::init() internally,
     * then drives Boot → StaConnecting → StaConnected / ApPortal.
     * api_server::begin() is called from net_manager on STA connect. */
    net_manager::init();

    /* ── Claude auth init (profile validation) ───────────────────── */
    claude_auth::init();

#ifdef FB_STREAM
    /* Live framebuffer streamer (debug only) — start after WiFi/net is up. */
    fb_stream::init();
#endif

    /* ── Build and load the launcher screen ─────────────────────── */
    lv_obj_t* home = launcher::create_screen();
    Serial.printf("[main] launcher created scr=%p\n", (void*)home);
    screen_router::push(home);
    Serial.printf("[main] after router push(home): active=%p\n", (void*)lv_scr_act());
    lv_scr_load(home);
    Serial.printf("[main] after lv_scr_load(home): active=%p\n", (void*)lv_scr_act());

    LOG_I("main", "UI ready. Free heap=%lu", (unsigned long)esp_get_free_heap_size());

    Serial.println("[main] Serial commands: tdbg | info | reset");
    Serial.println("[main]   wifi: add | remove | list | prefer | clear | status");
    Serial.println("[main]   claude: profile add/remove/list/use | token set | refresh | poll | get");
    Serial.println("[main]   api: set-secret <secret> | status");
    Serial.println("[main]   mirror: on | off | status");
    Serial.println("[main]   touch <x> <y>  — inject synthetic tap (LVGL coords 0..319, 0..479)");
}

/* ── Quote-aware argument splitter ──────────────────────────────────
 * Splits "verb arg1 arg2" respecting double-quoted tokens.
 * e.g. 'wifi add "my ssid" "pass word"' → ["wifi","add","my ssid","pass word"]
 */
static void split_args(const String& line, String* argv, int max_argc, int* out_argc) {
    *out_argc = 0;
    int i = 0;
    int n = line.length();
    while (i < n && *out_argc < max_argc) {
        while (i < n && line[i] == ' ') i++;
        if (i >= n) break;
        String token = "";
        if (line[i] == '"') {
            i++;
            while (i < n && line[i] != '"') token += line[i++];
            if (i < n) i++;
        } else {
            while (i < n && line[i] != ' ') token += line[i++];
        }
        argv[(*out_argc)++] = token;
    }
}

/* ── Dispatch a complete serial command line ─────────────────────── */
static void dispatch_serial_cmd(const String& cmd) {
    static const int MAX_ARGS = 7;
    String argv[MAX_ARGS];
    int argc = 0;
    split_args(cmd, argv, MAX_ARGS, &argc);
    if (argc == 0) return;

    String verb = argv[0];
    verb.toLowerCase();

    /* ── Legacy single-word commands ─────────────────────────────── */
    if (verb == "cal") {
        Serial.println("[main] 'cal' is disabled — touch uses factory defaults");

    } else if (verb == "tdbg") {
        bool next = !touch::get_debug();
        touch::set_debug(next);
        Serial.printf("[main] touch debug: %s\n", next ? "ON" : "OFF");

    } else if (verb == "info") {
        Serial.printf("[main] Free heap: %lu  Min free heap: %lu\n",
                      (unsigned long)esp_get_free_heap_size(),
                      (unsigned long)esp_get_minimum_free_heap_size());
        Serial.printf("[main] Uptime: %lu ms\n", (unsigned long)millis());
        Serial.printf("[main] Touch debug: %s\n", touch::get_debug() ? "ON" : "OFF");

    } else if (verb == "reset") {
        Serial.println("[main] Resetting via serial command...");
        delay(100);
        ESP.restart();

    /* ── WiFi commands ────────────────────────────────────────────── */
    } else if (verb == "wifi") {
        if (argc < 2) {
            Serial.println("[wifi] Usage: wifi <add|remove|list|prefer|clear|status>");
            return;
        }
        String sub = argv[1];
        sub.toLowerCase();

        if (sub == "add") {
            if (argc < 4) {
                Serial.println("[wifi] Usage: wifi add <ssid> <pass>");
                Serial.println("[wifi] Tip:   wifi add \"ssid with spaces\" \"pass word\"");
                return;
            }
            uint8_t idx = wifi_profiles::add_network(argv[2].c_str(), argv[3].c_str());
            if (idx == 255) {
                Serial.println("[wifi] Error: could not add network (max 4 reached?)");
            } else {
                Serial.printf("[wifi] Network added: index=%u ssid=%s\n", idx, argv[2].c_str());
            }

        } else if (sub == "remove") {
            if (argc < 3) {
                Serial.println("[wifi] Usage: wifi remove <ssid>");
                return;
            }
            if (wifi_profiles::remove_network(argv[2].c_str())) {
                Serial.printf("[wifi] Removed: %s\n", argv[2].c_str());
            } else {
                Serial.printf("[wifi] Not found: %s\n", argv[2].c_str());
            }

        } else if (sub == "list") {
            uint8_t count = wifi_profiles::network_count();
            if (count == 0) {
                Serial.println("[wifi] No networks configured");
                return;
            }
            String cur = wifi_profiles::get_ssid();
            for (uint8_t i = 0; i < count; i++) {
                wifi_profiles::Network net;
                if (!wifi_profiles::load_network(i, net)) continue;
                bool connected = wifi_profiles::is_connected() &&
                                 cur.equalsIgnoreCase(net.ssid);
                Serial.printf("[wifi] %u: %s (prio=%u)%s\n",
                              i, net.ssid, net.prio,
                              connected ? " [connected]" : "");
            }

        } else if (sub == "prefer") {
            if (argc < 3) {
                Serial.println("[wifi] Usage: wifi prefer <ssid>");
                return;
            }
            if (wifi_profiles::prefer_network(argv[2].c_str())) {
                Serial.printf("[wifi] %s set to highest priority\n", argv[2].c_str());
            } else {
                Serial.printf("[wifi] Not found: %s\n", argv[2].c_str());
            }

        } else if (sub == "clear") {
            wifi_profiles::clear_all();
            Serial.println("[wifi] All networks cleared, WiFi disconnected");

        } else if (sub == "status") {
            if (wifi_profiles::is_connected()) {
                Serial.printf("[wifi] Connected  ssid=%s  rssi=%d dBm  ip=%s\n",
                              wifi_profiles::get_ssid().c_str(),
                              wifi_profiles::get_rssi(),
                              wifi_profiles::get_ip().c_str());
            } else {
                Serial.printf("[wifi] Disconnected  (stored networks: %u)\n",
                              wifi_profiles::network_count());
            }

        } else if (sub == "set") {
            /* Legacy command — redirect to 'wifi add' */
            if (argc < 4) {
                Serial.println("[wifi] 'wifi set' is deprecated. Use: wifi add <ssid> <pass>");
                return;
            }
            Serial.println("[wifi] Note: 'wifi set' is deprecated, use 'wifi add' for multi-network support");
            uint8_t idx = wifi_profiles::add_network(argv[2].c_str(), argv[3].c_str());
            if (idx != 255) {
                Serial.printf("[wifi] Network added at index %u\n", idx);
            }

        } else {
            Serial.printf("[wifi] Unknown subcommand: '%s'\n", sub.c_str());
            Serial.println("[wifi] Subcommands: add | remove | list | prefer | clear | status");
        }

    /* ── Claude commands ─────────────────────────────────────────── */
    } else if (verb == "claude") {
        if (argc < 2) {
            Serial.println("[claude] Usage: claude <profile|token|poll|get|set>");
            return;
        }
        String sub = argv[1];
        sub.toLowerCase();

        /* ── claude profile ... ─────────────────────────────────── */
        if (sub == "profile") {
            if (argc < 3) {
                Serial.println("[claude] Usage: claude profile <add|remove|list|use> [<label>]");
                return;
            }
            String psub = argv[2];
            psub.toLowerCase();

            if (psub == "add") {
                if (argc < 4) {
                    Serial.println("[claude] Usage: claude profile add <label>");
                    return;
                }
                uint8_t idx = claude_auth::add_profile(argv[3].c_str());
                if (idx == 255) {
                    Serial.printf("[claude] Error: could not add profile '%s' (duplicate or max reached)\n",
                                  argv[3].c_str());
                } else {
                    Serial.printf("[claude] Profile added: index=%u label=%s\n",
                                  idx, argv[3].c_str());
                }

            } else if (psub == "remove") {
                if (argc < 4) {
                    Serial.println("[claude] Usage: claude profile remove <label>");
                    return;
                }
                uint8_t idx = claude_auth::find_by_label(argv[3].c_str());
                if (idx == 255) {
                    Serial.printf("[claude] Not found: '%s'\n", argv[3].c_str());
                } else if (claude_auth::remove_profile(idx)) {
                    Serial.printf("[claude] Removed profile: %s\n", argv[3].c_str());
                } else {
                    Serial.printf("[claude] Error removing profile: %s\n", argv[3].c_str());
                }

            } else if (psub == "list") {
                uint8_t count = claude_auth::profile_count();
                uint8_t active = claude_auth::active_index();
                if (count == 0) {
                    Serial.println("[claude] No profiles configured");
                    return;
                }
                for (uint8_t i = 0; i < count; i++) {
                    claude_auth::Profile p;
                    if (!claude_auth::load_profile(i, p)) continue;
                    bool has_token = (p.access[0] != '\0');
                    Serial.printf("[claude] %s%u: %s %s\n",
                                  (i == active) ? "*" : " ",
                                  i, p.label,
                                  has_token ? "[token set]" : "[NO TOKEN]");
                }

            } else if (psub == "use") {
                if (argc < 4) {
                    Serial.println("[claude] Usage: claude profile use <label>");
                    return;
                }
                uint8_t idx = claude_auth::find_by_label(argv[3].c_str());
                if (idx == 255) {
                    Serial.printf("[claude] Not found: '%s'\n", argv[3].c_str());
                } else if (claude_auth::set_active(idx)) {
                    Serial.printf("[claude] Active profile: %s (index=%u)\n",
                                  argv[3].c_str(), idx);
                }
            } else {
                Serial.printf("[claude] Unknown profile subcommand: '%s'\n", psub.c_str());
            }

        /* ── claude token set <label> <access> <refresh> <expires_sec> ── */
        } else if (sub == "token") {
            if (argc < 3) {
                Serial.println("[claude] Usage: claude token set <label> <access> <refresh> <expires_unix_sec>");
                return;
            }
            String tsub = argv[2];
            tsub.toLowerCase();

            if (tsub == "set") {
                if (argc < 7) {
                    Serial.println("[claude] Usage: claude token set <label> <access> <refresh> <expires_unix_sec>");
                    Serial.println("[claude] Note:  expires_unix_sec from credentials.json expiresAt (÷1000 if in ms)");
                    Serial.println("[claude]        Pass 0 if unknown (assumes +1 hour).");
                    return;
                }
                uint8_t idx = claude_auth::find_by_label(argv[3].c_str());
                if (idx == 255) {
                    Serial.printf("[claude] Profile not found: '%s'\n", argv[3].c_str());
                    return;
                }
                int64_t expires = (int64_t)argv[6].toInt();
                if (claude_auth::set_tokens(idx, argv[4].c_str(), argv[5].c_str(), expires)) {
                    Serial.printf("[claude] Tokens set for profile '%s' (index=%u)\n",
                                  argv[3].c_str(), idx);
                } else {
                    Serial.printf("[claude] Error setting tokens for profile '%s'\n", argv[3].c_str());
                }
            } else {
                Serial.printf("[claude] Unknown token subcommand: '%s'\n", tsub.c_str());
            }

        /* ── claude refresh ─────────────────────────────────────── */
        } else if (sub == "refresh") {
            Serial.println("[claude] Attempting OAuth token refresh...");
            if (claude_auth::refresh_active()) {
                Serial.println("[claude] Token refresh OK — new token stored in NVS");
            } else {
                Serial.println("[claude] Token refresh FAILED — check WiFi and logs");
            }

        /* ── claude poll ─────────────────────────────────────────── */
        } else if (sub == "poll") {
            Serial.println("[claude] Forcing immediate poll...");
            claude_widget::poll_now();

        /* ── claude get ──────────────────────────────────────────── */
        } else if (sub == "get") {
            String label = claude_auth::get_active_label();
            uint8_t count = claude_auth::profile_count();
            Serial.printf("[claude] Active profile: %s  (%u profiles total)\n",
                          label.c_str(), count);
            Serial.printf("[claude] Endpoint: %s\n",
                          "https://api.anthropic.com/api/oauth/usage (hardcoded)");
            bool expired = claude_auth::is_token_expired();
            Serial.printf("[claude] Token status: %s\n",
                          expired ? "EXPIRED" : "valid (or unknown)");

        /* ── claude set <url>  — deprecated ──────────────────────── */
        } else if (sub == "set") {
            Serial.println("[claude] WARNING: 'claude set' is deprecated and is now a no-op.");
            Serial.println("[claude] The endpoint is hardcoded to https://api.anthropic.com/api/oauth/usage");
            Serial.println("[claude] Use 'claude profile add' and 'claude token set' instead.");

        } else {
            Serial.printf("[claude] Unknown subcommand: '%s'\n", sub.c_str());
            Serial.println("[claude] Subcommands: profile | token | poll | get | set(deprecated)");
        }

    /* ── API server commands ─────────────────────────────────────── */
    } else if (verb == "api") {
        if (argc < 2) {
            Serial.println("[api] Usage: api <set-secret|status>");
            return;
        }
        String sub = argv[1];
        sub.toLowerCase();

        if (sub == "set-secret") {
            if (argc < 3) {
                Serial.println("[api] Usage: api set-secret <secret>");
                Serial.println("[api]        Use empty string (\"\") to clear.");
                return;
            }
            api_server::set_secret(argv[2].c_str());
            Serial.printf("[api] Secret %s\n",
                          argv[2].length() > 0 ? "updated" : "cleared");

        } else if (sub == "status") {
            Serial.printf("[api] %s\n", api_server::status_str().c_str());

        } else {
            Serial.printf("[api] Unknown subcommand: '%s'\n", sub.c_str());
            Serial.println("[api] Subcommands: set-secret | status");
        }

    /* ── Net manager commands ───────────────────────────────────── */
    } else if (verb == "net") {
        if (argc < 2) {
            Serial.println("[net] Usage: net <status|portal>");
            return;
        }
        String sub = argv[1];
        sub.toLowerCase();

        if (sub == "status") {
            const char* mode_name = "unknown";
            switch (net_manager::mode()) {
                case net_manager::Mode::Boot:          mode_name = "Boot";          break;
                case net_manager::Mode::StaConnecting: mode_name = "StaConnecting"; break;
                case net_manager::Mode::StaConnected:  mode_name = "StaConnected";  break;
                case net_manager::Mode::ApPortal:      mode_name = "ApPortal";      break;
                case net_manager::Mode::ApStaRetry:    mode_name = "ApStaRetry";    break;
            }
            Serial.printf("[net] mode=%s  ip=%s  ap_ssid=%s  portal=%s\n",
                          mode_name,
                          net_manager::ip_string().c_str(),
                          net_manager::ap_ssid(),
                          web_portal::active() ? "active" : "inactive");

        } else if (sub == "portal") {
            net_manager::force_portal();
            Serial.printf("[net] Captive portal forced — connect to '%s'\n",
                          net_manager::ap_ssid());

        } else {
            Serial.printf("[net] Unknown subcommand: '%s'\n", sub.c_str());
            Serial.println("[net] Subcommands: status | portal");
        }

    /* ── Touch inject command ────────────────────────────────────────── */
    } else if (verb == "touch") {
        if (argc < 3) {
            Serial.println("[touch] Usage: touch <x> <y>  (LVGL coords: x=0..319, y=0..479)");
            return;
        }
        int tx = argv[1].toInt();
        int ty = argv[2].toInt();
        if (tx < 0) tx = 0; else if (tx > 319) tx = 319;
        if (ty < 0) ty = 0; else if (ty > 479) ty = 479;
        touch::inject((int16_t)tx, (int16_t)ty);
        Serial.printf("[touch] inject (%d,%d)\n", tx, ty);

    /* ── Mirror commands ────────────────────────────────────────────── */
    } else if (verb == "mirror") {
        String sub = (argc >= 2) ? argv[1] : String("status");
        sub.toLowerCase();

        if (sub == "on") {
            config::MirrorCfg m = config::mirror();
            m.enabled = true;
            config::set_mirror(m);
            screen_mirror::enable(true);
            Serial.printf("[mirror] ON (interval=%d out=%dx%d)\n",
                          m.interval_ms, m.out_width, m.out_height);

        } else if (sub == "off") {
            config::MirrorCfg m = config::mirror();
            m.enabled = false;
            config::set_mirror(m);
            screen_mirror::enable(false);
            Serial.println("[mirror] OFF");

        } else if (sub == "status") {
            const config::MirrorCfg& m = config::mirror();
            Serial.printf("[mirror] %s  interval=%d  out=%dx%d  cap=80x120\n",
                          screen_mirror::enabled() ? "ON" : "OFF",
                          m.interval_ms, m.out_width, m.out_height);

        } else {
            Serial.println("[mirror] usage: mirror <on|off|status>");
        }

    } else {
        Serial.printf("[main] Unknown command: '%s'\n", cmd.c_str());
        Serial.println("[main] Commands: cal | tdbg | info | reset | wifi | claude | api | net | mirror | touch");
    }
}

/* Track active screen pointer across loop iterations */
static lv_obj_t* s_last_active_scr = nullptr;
static int s_loop_count = 0;

void loop() {
    lv_timer_handler();

    /* net_manager drives WiFi state machine + DNS pump + web portal.
     * net_manager::loop() calls web_portal::handle() unconditionally at its
     * entry before any switch/return, so it is pumped exactly once per loop.
     * Must be called unconditionally — never early-return before this. */
    net_manager::loop();

    /* api_server pumps its WebServer(:8080) — started by net_manager
     * on STA connect; handle() is a no-op until begin() has been called. */
    api_server::handle();

#ifdef FB_STREAM
    fb_stream::loop();   /* accept viewer + push a full frame on connect */
#endif

    if (s_loop_count < 100) {
        s_loop_count++;
        lv_obj_t* cur = lv_scr_act();
        if (cur != s_last_active_scr) {
            Serial.printf("[loop #%d] screen changed: %p -> %p\n",
                          s_loop_count, (void*)s_last_active_scr, (void*)cur);
            s_last_active_scr = cur;
        }
    }

    /* ── Non-blocking serial command reader ─────────────────────── */
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            s_serial_buf.trim();
            if (s_serial_buf.length() > 0) {
                dispatch_serial_cmd(s_serial_buf);
            }
            s_serial_buf = "";
        } else {
            s_serial_buf += c;
        }
    }

    delay(5);
}

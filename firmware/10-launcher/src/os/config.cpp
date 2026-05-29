#include "config.h"
#include "logger.h"
#include "sd_store.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

/**
 * Config system — ArduinoJson 7 + LittleFS /config.json
 *
 * Boot sequence:
 *   1. mount_fs()         → LittleFS.begin(true)
 *   2. load_config()      → if /config.json missing → copy default
 *                           parse → merge with compiled defaults
 *                           parse error → use compiled defaults, keep file
 *
 * Compiled defaults mirror PLAN.md §6.
 */

/* ── Compiled-in defaults ──────────────────────────────────────────
 * Plain brace-init avoids C99 designated-initializer syntax which
 * g++ (C++11 mode) does not support for aggregate char arrays.
 * Order must match the struct member declaration order in config.h.
 */
static const config::DisplayCfg kDefaultDisplay = { 0, 80, 20, 60 };

/* NetworkCfg has char arrays — zero-init then set via helper below */
static config::NetworkCfg kDefaultNetwork;
static config::DeviceCfg  kDefaultDevice;
static config::AppsCfg    kDefaultApps;

static void init_defaults() {
    static bool done = false;
    if (done) return;
    done = true;
    strncpy(kDefaultNetwork.ssid,     "",           sizeof(kDefaultNetwork.ssid) - 1);
    strncpy(kDefaultNetwork.password, "",           sizeof(kDefaultNetwork.password) - 1);
    strncpy(kDefaultNetwork.hostname, "espscreen",  sizeof(kDefaultNetwork.hostname) - 1);
    strncpy(kDefaultDevice.name,      "EspScreen-01", sizeof(kDefaultDevice.name) - 1);
    strncpy(kDefaultDevice.mode,      "standalone", sizeof(kDefaultDevice.mode) - 1);
    strncpy(kDefaultApps.autostart,   "launcher",   sizeof(kDefaultApps.autostart) - 1);
}

/* ── Live config state ─────────────────────────────────────────── */
static config::DisplayCfg s_display = { 0, 80, 20, 60 };
static config::NetworkCfg s_network;
static config::DeviceCfg  s_device;
static config::AppsCfg    s_apps;

namespace config {

bool mount_fs() {
    /* Seed defaults into both kDefault* and s_* structs */
    init_defaults();
    s_display = kDefaultDisplay;
    s_network = kDefaultNetwork;
    s_device  = kDefaultDevice;
    s_apps    = kDefaultApps;

    if (!LittleFS.begin(true)) {
        LOG_E("config", "LittleFS.begin failed");
        return false;
    }
    LOG_I("config", "LittleFS mounted");
    return true;
}

/* Copy /config.default.json → /config.json */
static bool copy_default() {
    File src = LittleFS.open("/config.default.json", "r");
    if (!src) {
        LOG_E("config", "config.default.json not found in LittleFS");
        return false;
    }
    File dst = LittleFS.open("/config.json", "w");
    if (!dst) {
        src.close();
        LOG_E("config", "cannot create /config.json");
        return false;
    }
    while (src.available()) {
        dst.write(src.read());
    }
    src.close();
    dst.close();
    LOG_I("config", "Copied default → /config.json");
    return true;
}

/* Apply parsed JsonDocument to live structs, filling missing keys with defaults */
static void apply_doc(JsonDocument& doc) {
    init_defaults();   // idempotent (guarded); ensures kDefault* are populated
    /* device */
    const char* name = doc["device"]["name"] | kDefaultDevice.name;
    const char* mode = doc["device"]["mode"] | kDefaultDevice.mode;
    strncpy(s_device.name, name, sizeof(s_device.name) - 1);
    strncpy(s_device.mode, mode, sizeof(s_device.mode) - 1);

    /* display */
    s_display.rotation         = doc["display"]["rotation"]         | kDefaultDisplay.rotation;
    s_display.backlight_pct    = doc["display"]["backlight_pct"]    | kDefaultDisplay.backlight_pct;
    s_display.idle_dim_pct     = doc["display"]["idle_dim_pct"]     | kDefaultDisplay.idle_dim_pct;
    s_display.idle_timeout_sec = doc["display"]["idle_timeout_sec"] | kDefaultDisplay.idle_timeout_sec;

    /* network */
    const char* ssid = doc["network"]["wifi"]["ssid"]     | kDefaultNetwork.ssid;
    const char* pass = doc["network"]["wifi"]["password"] | kDefaultNetwork.password;
    const char* host = doc["network"]["wifi"]["hostname"] | kDefaultNetwork.hostname;
    strncpy(s_network.ssid,     ssid, sizeof(s_network.ssid) - 1);
    strncpy(s_network.password, pass, sizeof(s_network.password) - 1);
    strncpy(s_network.hostname, host, sizeof(s_network.hostname) - 1);

    /* apps */
    const char* autostart = doc["apps"]["autostart"] | kDefaultApps.autostart;
    strncpy(s_apps.autostart, autostart, sizeof(s_apps.autostart) - 1);
}

void load_config() {
    /* Ensure /config.json exists */
    if (!LittleFS.exists("/config.json")) {
        LOG_I("config", "/config.json missing — copying default");
        if (!copy_default()) {
            LOG_W("config", "Using compiled defaults");
            return;
        }
    }

    File f = LittleFS.open("/config.json", "r");
    if (!f) {
        LOG_E("config", "Cannot open /config.json");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        LOG_E("config", "JSON parse error: %s — using compiled defaults", err.c_str());
        return;
    }

    apply_doc(doc);
    LOG_I("config", "Loaded device.name=%s", s_device.name);
    LOG_I("config", "free heap after config=%lu", (unsigned long)esp_get_free_heap_size());
}

/* ── Typed accessors ─────────────────────────────────────────── */
const DisplayCfg& display() { return s_display; }
const NetworkCfg& network() { return s_network; }
const DeviceCfg&  device()  { return s_device;  }
const AppsCfg&    apps()    { return s_apps;     }

/* ── save_config ─────────────────────────────────────────────────
 * Key-preservation approach:
 *   1. Load the EXISTING /config.json from LittleFS into a JsonDocument.
 *      This captures all keys the firmware does not model (e.g. leds,
 *      security, apps.claude_widget, logging).
 *   2. Mutate ONLY the keys the firmware actually owns.
 *   3. Re-serialise back to /config.json — unknown keys survive intact.
 *
 * If the file is absent or unparseable we fall back to writing a fresh
 * document (same behaviour as having no unknown keys).
 */
bool save_config() {
    JsonDocument doc;

    /* Load existing file to preserve unknown keys */
    if (LittleFS.exists("/config.json")) {
        File f = LittleFS.open("/config.json", "r");
        if (f) {
            DeserializationError err = deserializeJson(doc, f);
            f.close();
            if (err) {
                LOG_W("config", "save_config: re-parse failed (%s), starting fresh", err.c_str());
                doc.clear();
            }
        }
    }

    /* Mutate only the keys this firmware models */
    doc["device"]["name"] = s_device.name;
    doc["device"]["mode"] = s_device.mode;

    doc["display"]["rotation"]         = s_display.rotation;
    doc["display"]["backlight_pct"]    = s_display.backlight_pct;
    doc["display"]["idle_dim_pct"]     = s_display.idle_dim_pct;
    doc["display"]["idle_timeout_sec"] = s_display.idle_timeout_sec;

    doc["network"]["wifi"]["ssid"]     = s_network.ssid;
    doc["network"]["wifi"]["password"] = s_network.password;
    doc["network"]["wifi"]["hostname"] = s_network.hostname;

    doc["apps"]["autostart"] = s_apps.autostart;

    /* Write to LittleFS atomically via tmp → rename */
    File lf = LittleFS.open("/config.json.tmp", "w");
    if (!lf) { LOG_E("config", "save: open tmp failed"); return false; }
    serializeJson(doc, lf);
    lf.close();
    if (LittleFS.exists("/config.json")) LittleFS.remove("/config.json");
    if (!LittleFS.rename("/config.json.tmp", "/config.json")) {
        LOG_E("config", "save: rename failed");
        return false;
    }
    LOG_I("config", "save_config: LittleFS written");

    /* Best-effort write to SD if available */
    if (sd_store::available()) {
        String sd_json;
        serializeJson(doc, sd_json);
        if (!sd_store::write_file("/espscreen/config.json", sd_json)) {
            LOG_W("config", "save_config: SD write failed (LittleFS ok)");
        }
    }

    return true;
}

/* ── import_from_sd_if_present ───────────────────────────────────
 * No-op if no card or no /espscreen/config.json on it.
 * Reuses the same apply_doc() path as load_config() for consistency.
 */
void import_from_sd_if_present() {
    if (!sd_store::available()) return;
    if (!sd_store::exists("/espscreen/config.json")) return;

    String json;
    if (!sd_store::read_file("/espscreen/config.json", json)) {
        LOG_W("config", "import_from_sd: read failed");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        LOG_E("config", "import_from_sd: parse error %s", err.c_str());
        return;
    }

    apply_doc(doc);
    LOG_I("config", "cfg src=SD");
}

} // namespace config

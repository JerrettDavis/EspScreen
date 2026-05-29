#pragma once
#include <Arduino.h>

namespace config {
    /** Mount LittleFS. Call before load_config(). */
    bool mount_fs();

    /**
     * Load /config.json from LittleFS.
     * If absent → copies config.default.json → /config.json first.
     * Parse error → uses compiled-in defaults, file kept for debug.
     * Unknown keys in the file are preserved.
     */
    void load_config();

    /* ── Typed accessor structs ───────────────────────────────── */
    struct DisplayCfg {
        int  rotation;
        int  backlight_pct;
        int  idle_dim_pct;
        int  idle_timeout_sec;
    };
    struct NetworkCfg {
        char ssid[64];
        char password[64];
        char hostname[32];
    };
    struct DeviceCfg {
        char name[64];
        char mode[16];
    };
    struct AppsCfg {
        char autostart[32];
    };

    const DisplayCfg& display();
    const NetworkCfg& network();
    const DeviceCfg&  device();
    const AppsCfg&    apps();

    /**
     * save_config() — persist the current config to LittleFS /config.json.
     *
     * Loads the EXISTING /config.json first, mutates only the keys the
     * firmware models, then re-serialises — so unknown keys (apps.claude_widget,
     * leds, security, logging, etc.) are preserved, not dropped.
     *
     * If sd_store::available(), also writes /espscreen/config.json to the SD
     * card as a best-effort backup (failure → LOG_W, not a hard error).
     *
     * Returns true if the LittleFS write succeeded.
     */
    bool save_config();

    /**
     * import_from_sd_if_present() — if an SD card is mounted and
     * /espscreen/config.json exists on it, read and apply that config.
     *
     * Call AFTER sd_store::init().  If no card or no file, this is a no-op
     * (the LittleFS config loaded at boot stays in effect).
     * Logs "cfg src=SD" on success.
     */
    void import_from_sd_if_present();
}

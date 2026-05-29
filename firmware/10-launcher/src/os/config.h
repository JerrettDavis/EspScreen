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
}

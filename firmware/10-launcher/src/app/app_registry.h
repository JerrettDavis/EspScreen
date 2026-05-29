#pragma once
#include <lvgl.h>

/**
 * AppEntry — descriptor for a built-in app tile.
 * Phase 2/3 will extend with FS-scanned dynamic entries.
 */
struct AppEntry {
    const char* id;       // unique key, e.g. "launcher"
    const char* label;    // display name
    const char* icon;     // UTF-8 symbol or LV_SYMBOL_* string
    lv_obj_t* (*create_screen)();  // factory — called on first tap
};

namespace app_registry {
    /**
     * Compile-time array of built-in apps.
     * Launcher iterates this to build the tile grid.
     */
    extern const AppEntry kBuiltinApps[];
    extern const int kBuiltinAppCount;
}

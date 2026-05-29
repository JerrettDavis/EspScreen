#include "nvs_store.h"
#include <Preferences.h>

/**
 * NVS store — wraps ESP32 Preferences for namespace-scoped access.
 * Each call opens and closes the namespace to keep it simple and safe.
 * For batch writes (e.g. calibration) callers should group them quickly.
 *
 * NVS key length limit: 15 chars (ESP-IDF constraint).
 */

namespace nvs_store {

/* ── int32 ─────────────────────────────────────────────────────── */

void put_i32(const char* ns, const char* key, int32_t val) {
    Preferences prefs;
    prefs.begin(ns, false);  // read-write
    prefs.putInt(key, val);
    prefs.end();
}

int32_t get_i32(const char* ns, const char* key, int32_t default_val) {
    Preferences prefs;
    prefs.begin(ns, true);   // read-only
    int32_t v = prefs.getInt(key, default_val);
    prefs.end();
    return v;
}

/* ── uint8 ─────────────────────────────────────────────────────── */

void put_u8(const char* ns, const char* key, uint8_t val) {
    Preferences prefs;
    prefs.begin(ns, false);
    prefs.putUChar(key, val);
    prefs.end();
}

uint8_t get_u8(const char* ns, const char* key, uint8_t default_val) {
    Preferences prefs;
    prefs.begin(ns, true);
    uint8_t v = prefs.getUChar(key, default_val);
    prefs.end();
    return v;
}

/* ── string ─────────────────────────────────────────────────────  */

void put_str(const char* ns, const char* key, const char* val) {
    Preferences prefs;
    prefs.begin(ns, false);
    prefs.putString(key, val);
    prefs.end();
}

String get_str(const char* ns, const char* key, const char* default_val) {
    Preferences prefs;
    prefs.begin(ns, true);
    String v = prefs.getString(key, default_val);
    prefs.end();
    return v;
}

} // namespace nvs_store

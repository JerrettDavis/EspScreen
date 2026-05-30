/**
 * sim_stubs.cpp — host symbol definitions shared by all simulated screens.
 *
 * P2: the REAL screen .cpp files and the REAL screen_router.cpp are now compiled
 * in, so this file no longer stubs those. It defines:
 *   - the Arduino Serial singleton
 *   - wifi_profiles::* (full surface; the screens only call a few at build time)
 *   - the project logger (logger_log → Serial)
 *
 * Other subsystems (claude_auth, net_manager, nvs_store) and the FreeRTOS / ESP
 * / WiFi / HTTP headers live in their own stub files under sim/stubs/.
 */
#include <Arduino.h>
#include <lvgl.h>
#include <cstdio>

#include "../../src/os/wifi_profiles.h"
#include "../../src/os/logger.h"

/* ── Arduino Serial singleton (declared extern in stubs/Arduino.h) ─────────── */
HardwareSerial Serial;

/* ── Project logger → Serial ───────────────────────────────────────────────── */
void logger_init() {}
void logger_log(const char* level, const char* tag, const char* msg) {
    Serial.printf("[%s %s] %s\n", tag ? tag : "?", level ? level : "?", msg ? msg : "");
}

/* ── wifi_profiles — fake two stored networks, "connected" state. ──────────── */
namespace wifi_profiles {
    void    init() {}
    bool    is_connected() { return true; }
    int     get_rssi() { return -50; }
    String  get_ip() { return String("192.168.1.42"); }
    String  get_ssid() { return String("net-0"); }
    uint8_t network_count() { return 2; }

    bool load_network(uint8_t idx, Network& out) {
        if (idx >= 2) return false;
        out.index = idx;
        std::snprintf(out.ssid, sizeof(out.ssid), "net-%u", (unsigned)idx);
        out.pass[0] = '\0';
        out.prio = idx;
        return true;
    }
    uint8_t find_by_ssid(const char*) { return 255; }
    uint8_t add_network(const char*, const char*) { return 255; }
    bool    remove_network(const char*) { return false; }
    bool    prefer_network(const char*) { return false; }
    void    clear_all() {}

    uint8_t scan(ScanResult* out, uint8_t max) {
        /* Return zero results so wifi_setup's scan list renders the
         * "No networks found" branch deterministically. */
        (void)out; (void)max;
        return 0;
    }
    bool connect_now(const char*, const char*, uint32_t) { return false; }
}

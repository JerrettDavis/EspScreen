/**
 * wifi_mgr.cpp — NVS-backed WiFi credential management.
 *
 * - Reads SSID/pass from NVS namespace "wifi", keys "ssid"/"pass"
 * - Non-blocking 12s connect attempt with status logging
 * - Auto-reconnect via WiFi library (we manage NVS persistence, not WiFi lib's store)
 * - NTP sync triggered automatically on successful connect
 */

#include "wifi_mgr.h"
#include "nvs_store.h"
#include "logger.h"
#include <WiFi.h>
#include <Arduino.h>
#include <time.h>

namespace wifi_mgr {

static const char* NVS_NS   = "wifi";
static const char* KEY_SSID = "ssid";
static const char* KEY_PASS = "pass";

/* ── NTP ────────────────────────────────────────────────────────── */
static void start_ntp() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    LOG_I("wifi", "NTP sync started (pool.ntp.org / time.nist.gov)");
}

/* ── Public API ─────────────────────────────────────────────────── */

void init() {
    String ssid = nvs_store::get_str(NVS_NS, KEY_SSID, "");
    String pass = nvs_store::get_str(NVS_NS, KEY_PASS, "");

    if (ssid.isEmpty()) {
        Serial.println("[wifi] Not provisioned — type 'wifi set <ssid> <pass>' over serial");
        return;
    }

    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);   // We manage persistence in NVS, not WiFi lib's store

    Serial.printf("[wifi] connecting to %s...\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());

    /* Non-blocking 12s polling window */
    const uint32_t kTimeoutMs = 12000;
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < kTimeoutMs) {
        delay(200);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        LOG_I("wifi", "Connected! IP=%s RSSI=%d dBm",
              WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
        start_ntp();
    } else {
        LOG_W("wifi", "Connect timeout after 12s (ssid=%s) — will auto-reconnect in background",
              ssid.c_str());
    }
}

bool is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

int get_rssi() {
    if (WiFi.status() == WL_CONNECTED) {
        return (int)WiFi.RSSI();
    }
    return 0;
}

String get_ip() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return "";
}

String get_ssid() {
    return nvs_store::get_str(NVS_NS, KEY_SSID, "");
}

void set_credentials(const char* ssid, const char* pass) {
    nvs_store::put_str(NVS_NS, KEY_SSID, ssid);
    nvs_store::put_str(NVS_NS, KEY_PASS, pass);
    LOG_I("wifi", "Credentials saved (ssid=%s)", ssid);
}

void clear_credentials() {
    nvs_store::put_str(NVS_NS, KEY_SSID, "");
    nvs_store::put_str(NVS_NS, KEY_PASS, "");
    WiFi.disconnect(true);
    LOG_I("wifi", "Credentials cleared, WiFi disconnected");
}

} // namespace wifi_mgr

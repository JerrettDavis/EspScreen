/**
 * wifi_profiles.cpp — Multi-network WiFi storage with priority-ordered connection.
 *
 * Handles legacy migration from old single-credential schema automatically.
 */

#include "wifi_profiles.h"
#include "nvs_store.h"
#include "logger.h"
#include <WiFi.h>
#include <Arduino.h>
#include <time.h>

namespace wifi_profiles {

/* ── NVS helpers ─────────────────────────────────────────────────────── */

static const char* NS_GLOBAL  = "wifi";
static const char* KEY_COUNT  = "count";

/* Legacy single-credential keys (old wifi_mgr schema) */
static const char* KEY_LEG_SSID = "ssid";
static const char* KEY_LEG_PASS = "pass";

static void net_ns(uint8_t idx, char* buf, size_t n) {
    snprintf(buf, n, "wf_n%u", (unsigned)idx);
}

/* ── NTP ─────────────────────────────────────────────────────────────── */

static void start_ntp() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    LOG_I("wifi", "NTP sync started");
}

/* ── Migration ───────────────────────────────────────────────────────── */

static void migrate_legacy() {
    /* Check if old schema exists AND new schema isn't already written */
    String old_ssid = nvs_store::get_str(NS_GLOBAL, KEY_LEG_SSID, "");
    String count_str = nvs_store::get_str(NS_GLOBAL, KEY_COUNT, "MISSING");

    /* count is u8 — if it hasn't been written, get_u8 returns default 0.
     * We detect "never written" by checking if the old ssid key exists. */
    bool has_new_schema = (nvs_store::get_u8(NS_GLOBAL, KEY_COUNT, 255) != 255);

    if (!old_ssid.isEmpty() && !has_new_schema) {
        String old_pass = nvs_store::get_str(NS_GLOBAL, KEY_LEG_PASS, "");
        Serial.printf("[wifi] Legacy credentials found (ssid=%s) — migrating to multi-network schema\n",
                      old_ssid.c_str());
        /* Write to slot 0 */
        char ns[8];
        net_ns(0, ns, sizeof(ns));
        nvs_store::put_str(ns, "ssid", old_ssid.c_str());
        nvs_store::put_str(ns, "pass", old_pass.c_str());
        nvs_store::put_u8(ns, "prio", 0);
        nvs_store::put_u8(NS_GLOBAL, KEY_COUNT, 1);
        LOG_I("wifi", "Migration complete: wf_n0 = %s", old_ssid.c_str());
        /* Note: leaving old keys for safety — they are harmless */
    }
}

/* ── Connection logic ────────────────────────────────────────────────── */

struct NetEntry {
    char    ssid[64];
    char    pass[64];
    uint8_t prio;
    uint8_t idx;
};

/* Sort by priority ascending (lower prio value = higher priority) */
static void sort_by_prio(NetEntry* entries, uint8_t count) {
    for (uint8_t i = 0; i < count - 1; i++) {
        for (uint8_t j = i + 1; j < count; j++) {
            if (entries[j].prio < entries[i].prio) {
                NetEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }
}

static bool try_connect(const char* ssid, const char* pass, uint32_t timeout_ms) {
    WiFi.disconnect(false);
    WiFi.begin(ssid, pass);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
        delay(200);
        Serial.print(".");
    }
    Serial.println();
    return (WiFi.status() == WL_CONNECTED);
}

/* ── Public API ──────────────────────────────────────────────────────── */

void init() {
    /* 1. Run legacy migration if needed */
    migrate_legacy();

    uint8_t count = nvs_store::get_u8(NS_GLOBAL, KEY_COUNT, 0);
    if (count == 0) {
        Serial.println("[wifi] No networks stored. Use: wifi add <ssid> <pass>");
        return;
    }

    /* 2. Load all networks and sort by priority */
    NetEntry entries[MAX_NETWORKS];
    uint8_t loaded = 0;
    for (uint8_t i = 0; i < count && i < MAX_NETWORKS; i++) {
        char ns[8];
        net_ns(i, ns, sizeof(ns));
        String ssid = nvs_store::get_str(ns, "ssid", "");
        if (ssid.isEmpty()) continue;
        String pass = nvs_store::get_str(ns, "pass", "");
        strlcpy(entries[loaded].ssid, ssid.c_str(), sizeof(entries[0].ssid));
        strlcpy(entries[loaded].pass, pass.c_str(), sizeof(entries[0].pass));
        entries[loaded].prio = nvs_store::get_u8(ns, "prio", i);
        entries[loaded].idx = i;
        loaded++;
    }
    sort_by_prio(entries, loaded);

    /* 3. Try each network in priority order */
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);

    for (uint8_t i = 0; i < loaded; i++) {
        Serial.printf("[wifi] Trying network %u/%u: %s (prio=%u)...\n",
                      i + 1, loaded, entries[i].ssid, entries[i].prio);
        if (try_connect(entries[i].ssid, entries[i].pass, 12000)) {
            LOG_I("wifi", "Connected to %s  IP=%s  RSSI=%d dBm",
                  entries[i].ssid,
                  WiFi.localIP().toString().c_str(),
                  (int)WiFi.RSSI());
            start_ntp();
            return;
        }
        LOG_W("wifi", "Failed to connect to %s", entries[i].ssid);
    }

    LOG_W("wifi", "All %u network(s) failed — will auto-reconnect in background", loaded);
}

bool is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

int get_rssi() {
    return (WiFi.status() == WL_CONNECTED) ? (int)WiFi.RSSI() : 0;
}

String get_ip() {
    return (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
}

String get_ssid() {
    if (WiFi.status() == WL_CONNECTED) return WiFi.SSID();
    /* Fall back to the highest-priority stored SSID */
    uint8_t count = nvs_store::get_u8(NS_GLOBAL, KEY_COUNT, 0);
    if (count == 0) return "";
    char ns[8];
    net_ns(0, ns, sizeof(ns));
    return nvs_store::get_str(ns, "ssid", "");
}

uint8_t network_count() {
    return nvs_store::get_u8(NS_GLOBAL, KEY_COUNT, 0);
}

bool load_network(uint8_t idx, Network& out) {
    if (idx >= network_count()) return false;
    char ns[8];
    net_ns(idx, ns, sizeof(ns));
    out.index = idx;
    String ssid = nvs_store::get_str(ns, "ssid", "");
    strlcpy(out.ssid, ssid.c_str(), sizeof(out.ssid));
    String pass = nvs_store::get_str(ns, "pass", "");
    strlcpy(out.pass, pass.c_str(), sizeof(out.pass));
    out.prio = nvs_store::get_u8(ns, "prio", idx);
    return true;
}

uint8_t find_by_ssid(const char* ssid) {
    uint8_t count = network_count();
    for (uint8_t i = 0; i < count; i++) {
        char ns[8];
        net_ns(i, ns, sizeof(ns));
        String s = nvs_store::get_str(ns, "ssid", "");
        if (s.equalsIgnoreCase(ssid)) return i;
    }
    return 255;
}

uint8_t add_network(const char* ssid, const char* pass) {
    /* Update if SSID already exists */
    uint8_t existing = find_by_ssid(ssid);
    if (existing != 255) {
        char ns[8];
        net_ns(existing, ns, sizeof(ns));
        nvs_store::put_str(ns, "pass", pass);
        LOG_I("wifi", "add_network: updated password for %s at index %u", ssid, existing);
        return existing;
    }

    uint8_t count = network_count();
    if (count >= MAX_NETWORKS) {
        LOG_W("wifi", "add_network: max networks reached (%u)", MAX_NETWORKS);
        return 255;
    }

    char ns[8];
    net_ns(count, ns, sizeof(ns));
    nvs_store::put_str(ns, "ssid", ssid);
    nvs_store::put_str(ns, "pass", pass);
    nvs_store::put_u8(ns, "prio", count);  // default priority = append order
    nvs_store::put_u8(NS_GLOBAL, KEY_COUNT, count + 1);
    LOG_I("wifi", "add_network: '%s' → index %u", ssid, count);
    return count;
}

bool remove_network(const char* ssid) {
    uint8_t idx = find_by_ssid(ssid);
    if (idx == 255) return false;

    uint8_t count = network_count();
    /* Shift entries down */
    for (uint8_t i = idx; i + 1 < count; i++) {
        char src[8], dst[8];
        net_ns(i + 1, src, sizeof(src));
        net_ns(i, dst, sizeof(dst));
        nvs_store::put_str(dst, "ssid", nvs_store::get_str(src, "ssid", "").c_str());
        nvs_store::put_str(dst, "pass", nvs_store::get_str(src, "pass", "").c_str());
        nvs_store::put_u8(dst, "prio", nvs_store::get_u8(src, "prio", i));
    }
    /* Clear last slot */
    char last_ns[8];
    net_ns(count - 1, last_ns, sizeof(last_ns));
    nvs_store::put_str(last_ns, "ssid", "");
    nvs_store::put_str(last_ns, "pass", "");
    nvs_store::put_u8(last_ns, "prio", 0);
    nvs_store::put_u8(NS_GLOBAL, KEY_COUNT, count - 1);
    LOG_I("wifi", "remove_network: '%s' removed", ssid);
    return true;
}

bool prefer_network(const char* ssid) {
    uint8_t target = find_by_ssid(ssid);
    if (target == 255) return false;
    uint8_t count = network_count();

    /* Set target to prio=0, increment all others */
    char ns[8];
    net_ns(target, ns, sizeof(ns));
    nvs_store::put_u8(ns, "prio", 0);
    for (uint8_t i = 0; i < count; i++) {
        if (i == target) continue;
        char nsi[8];
        net_ns(i, nsi, sizeof(nsi));
        uint8_t p = nvs_store::get_u8(nsi, "prio", i);
        nvs_store::put_u8(nsi, "prio", p + 1);
    }
    LOG_I("wifi", "prefer_network: '%s' set to priority 0", ssid);
    return true;
}

void clear_all() {
    uint8_t count = network_count();
    for (uint8_t i = 0; i < count; i++) {
        char ns[8];
        net_ns(i, ns, sizeof(ns));
        nvs_store::put_str(ns, "ssid", "");
        nvs_store::put_str(ns, "pass", "");
        nvs_store::put_u8(ns, "prio", 0);
    }
    nvs_store::put_u8(NS_GLOBAL, KEY_COUNT, 0);
    WiFi.disconnect(true);
    LOG_I("wifi", "clear_all: all networks wiped, WiFi disconnected");
}

} // namespace wifi_profiles

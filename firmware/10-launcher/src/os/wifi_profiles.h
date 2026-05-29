#pragma once
#include <Arduino.h>
#include <stdint.h>

/**
 * wifi_profiles — Multi-network WiFi storage with priority ordering.
 *
 * NVS layout:
 *   Namespace "wifi"        → count (u8)
 *   Namespace "wf_n<N>"    → ssid (str), pass (str), prio (u8)
 *
 * Max 4 networks.  On boot, scans visible APs and tries stored networks
 * in ascending priority order (0 = highest priority).
 *
 * Migration: if old "wifi.ssid" key exists and "wifi.count" doesn't,
 * the existing creds are migrated to wf_n0 automatically.
 *
 * Provisioned via serial commands (see main.cpp):
 *   wifi add <ssid> <pass>
 *   wifi remove <ssid>
 *   wifi list
 *   wifi prefer <ssid>
 *   wifi clear
 *   wifi status
 */

namespace wifi_profiles {

static constexpr uint8_t MAX_NETWORKS = 4;

struct Network {
    uint8_t index;
    char    ssid[64];
    char    pass[64];
    uint8_t prio;  // lower = higher priority
};

/** Load stored networks and attempt connection in priority order.
 *  Also handles legacy single-credential migration. */
void init();

/** Returns true if currently connected. */
bool is_connected();

/** Returns RSSI in dBm, or 0 if not connected. */
int get_rssi();

/** Returns IP address string (empty if not connected). */
String get_ip();

/** Returns SSID of currently connected network, or stored primary SSID. */
String get_ssid();

/** Total number of stored networks. */
uint8_t network_count();

/** Load a network by index. Returns false if out of range. */
bool load_network(uint8_t idx, Network& out);

/** Find network by SSID. Returns 255 if not found. */
uint8_t find_by_ssid(const char* ssid);

/**
 * Add or update a network.
 * If SSID matches an existing entry, updates the password.
 * Returns index (0-3) or 255 on failure (max reached).
 */
uint8_t add_network(const char* ssid, const char* pass);

/** Remove a network by SSID. Returns false if not found. */
bool remove_network(const char* ssid);

/** Bump a network to highest priority (prio=0), re-number others. */
bool prefer_network(const char* ssid);

/** Wipe all stored networks. */
void clear_all();

/* ── Scan & on-demand connect (appended for wifi_setup flow) ─────────── */

/**
 * A single AP result from a synchronous WiFi scan.
 * enc == 0 means open (no password required).
 */
struct ScanResult {
    char    ssid[33];   ///< null-terminated, max 32 chars
    int8_t  rssi;       ///< signal strength in dBm (negative)
    uint8_t enc;        ///< 0 = open, non-zero = encrypted
};

/**
 * Perform a synchronous WiFi scan.
 * Deduplicates by SSID (keeps strongest RSSI), caps at `max` results,
 * then calls WiFi.scanDelete() to free scan memory.
 * NOTE: scanning briefly drops the STA link (~2-4 s) — acceptable.
 * @param out   Caller-supplied array of at least `max` ScanResult entries.
 * @param max   Maximum number of results to return (recommend ≤ 12).
 * @return      Number of entries written to `out`.
 */
uint8_t scan(ScanResult* out, uint8_t max);

/**
 * Connect to an AP immediately and, on success, persist credentials.
 * Wraps the file-static try_connect(); on success also calls add_network()
 * and triggers NTP sync.
 * @param ssid        Target network SSID.
 * @param pass        Password (pass "" or nullptr for open networks).
 * @param timeout_ms  How long to wait for association (default 12 s).
 * @return true if WL_CONNECTED within timeout_ms.
 */
bool connect_now(const char* ssid, const char* pass, uint32_t timeout_ms = 12000);

} // namespace wifi_profiles

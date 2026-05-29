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

} // namespace wifi_profiles

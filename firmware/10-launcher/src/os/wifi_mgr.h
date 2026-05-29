#pragma once
#include <Arduino.h>

/**
 * wifi_mgr — NVS-backed WiFi credential management.
 *
 * Provisioning via serial commands (see main.cpp dispatcher):
 *   wifi set <ssid> <pass>  — save credentials and (re)connect
 *   wifi clear              — wipe credentials, disconnect
 *   wifi status             — print current status
 *
 * No captive portal — serial-only provisioning.
 */
namespace wifi_mgr {
    /** Read NVS credentials and attempt to connect (non-blocking 12s window). */
    void init();

    /** Returns true if WiFi is currently connected. */
    bool is_connected();

    /** Returns RSSI in dBm, or 0 if not connected. */
    int get_rssi();

    /** Returns IP address string (empty if not connected). */
    String get_ip();

    /** Returns the stored SSID (empty if not provisioned). */
    String get_ssid();

    /** Write new credentials to NVS. Caller should call init() to reconnect. */
    void set_credentials(const char* ssid, const char* pass);

    /** Wipe NVS credentials and disconnect. */
    void clear_credentials();
}

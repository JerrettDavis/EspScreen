#pragma once
#include <Arduino.h>

/**
 * net_manager — WiFi + portal lifecycle state machine.
 *
 * Drives connectivity from boot through captive-portal setup to stable STA
 * connection.  Owns the api_server::begin() call (moves it out of the
 * main.cpp loop gate so it only fires once, on the STA-connected entry).
 *
 * State machine:
 *   Boot → StaConnecting → StaConnected  (happy path)
 *                       ↘ ApPortal → ApStaRetry → StaConnected
 *                                             ↘ ApPortal (retry fail)
 *   StaConnected: link lost >30 s → StaConnecting → (ApPortal if still down)
 *
 * Call net_manager::init() from setup() INSTEAD OF wifi_profiles::init().
 * Call net_manager::loop() unconditionally from loop().
 */

namespace net_manager {

enum class Mode {
    Boot,          ///< Initial state — transitions immediately in init()
    StaConnecting, ///< wifi_profiles::init() is running / retrying
    StaConnected,  ///< STA up; api_server + web_portal(:80) running
    ApPortal,      ///< AP mode — captive portal up, DNS pumping
    ApStaRetry,    ///< Keeping AP up while testing new credentials
};

/** Replace wifi_profiles::init() call in setup(). */
void init();

/** Drive the state machine — call unconditionally from loop(). */
void loop();

/** Current mode. */
Mode mode();

/** SSID used when in AP mode. */
const char* ap_ssid();

/** Current IP as human-readable string (STA or AP). */
String ip_string();

/**
 * Bring up the captive portal even while STA is connected.
 * Uses WIFI_AP_STA so the STA link stays up.
 */
void force_portal();

/** True if the most recent ApStaRetry attempt failed (for /api/status). */
bool last_retry_failed();

/**
 * Internal: called by web_portal on POST /api/wifi to initiate a connection
 * attempt while keeping the AP up.
 */
void _trigger_sta_retry(const char* ssid, const char* pass);

} // namespace net_manager

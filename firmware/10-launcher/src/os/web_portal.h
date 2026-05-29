#pragma once
#include <Arduino.h>

/**
 * web_portal — Browser-based configuration portal on port 80.
 *
 * Provides a single-page app for:
 *   - WiFi SSID scan + credential submission
 *   - Claude profile/token provisioning (paste-token path)
 *   - Status dashboard matching /api/health from api_server
 *
 * Port: 80 (SEPARATE from api_server's 8080 — two WebServer instances coexist fine)
 *
 * Routes:
 *   GET  /                      → single-page app (PORTAL_HTML from portal_assets.h)
 *   GET  /api/scan              → JSON array [{ssid,rssi,enc}, …] capped at 12
 *   POST /api/wifi              → {ssid,pass} → add_network + trigger connect
 *   GET  /api/status            → same shape as api_server /api/health + net mode/ip
 *   POST /api/claude/profile    → {label} → claude_auth::add_profile / set_active
 *   POST /api/claude/tokens     → {label,access,refresh,expires_at} → same store fn
 *
 * Captive portal catch-alls (only meaningful when ap_mode=true):
 *   GET  /generate_204          → 302 → http://192.168.4.1/
 *   GET  /hotspot-detect.html   → 302 → http://192.168.4.1/
 *   GET  /connecttest.txt       → 302 → http://192.168.4.1/
 *   GET  /ncsi.txt              → 302 → http://192.168.4.1/
 *   onNotFound                  → 302 → http://192.168.4.1/
 */

namespace web_portal {

/** Start the portal WebServer on port 80.
 *  ap_mode=true → also handles captive probe URLs + redirects. */
void begin(bool ap_mode);

/** Pump the WebServer (and DNS when ap_mode).  Call unconditionally from loop(). */
void handle();

/** Stop and delete the WebServer. */
void end();

/** True if begin() has been called and end() has not. */
bool active();

} // namespace web_portal

#pragma once
#include <Arduino.h>

/**
 * api_server — Tiny HTTP server for token push from the host-side creds-watcher.
 *
 * Port: 8080
 *
 * Routes:
 *   GET  /api/health            — device info + active profile + token expiry
 *   POST /api/claude/tokens     — push new tokens to NVS
 *
 * Auth:
 *   Optional shared secret stored in NVS namespace "espscreen", key "api_secret".
 *   If set, requests must carry:  X-EspScreen-Secret: <secret>
 *   If not set, all requests are accepted (with a warning log).
 *
 * Set secret via serial:  api set-secret <secret>
 *
 * POST /api/claude/tokens body (JSON):
 *   {
 *     "label":      "Default",       // must match an existing profile label
 *     "access":     "sk-ant-oat01-...",
 *     "refresh":    "sk-ant-ort01-...",
 *     "expires_at": 1716800000       // Unix seconds or ms (auto-detected)
 *   }
 *
 * Response 200:  { "ok": true }
 * Response 4xx:  { "error": "..." }
 */

namespace api_server {

/** Start the HTTP server.  Call after WiFi connects. */
void begin();

/** Handle incoming requests — call from loop(). */
void handle();

/** Set the shared secret.  Pass nullptr or "" to clear. */
void set_secret(const char* secret);

/** Get current status string for serial 'api status' command. */
String status_str();

} // namespace api_server

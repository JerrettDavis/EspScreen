/**
 * api_server.cpp — Tiny HTTP server (port 8080) for host-side token push.
 *
 * Uses WebServer from Arduino-ESP32 (bundled, no extra lib_dep).
 * Runs single-threaded on the Arduino loop — handle() must be called each loop.
 */

#include "api_server.h"
#include "claude_auth.h"
#include "nvs_store.h"
#include "logger.h"
#include "wifi_profiles.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <time.h>

namespace api_server {

static const int PORT = 8080;

/* NVS namespace / key for the shared secret */
static const char* NS_API     = "espscreen";
static const char* KEY_SECRET = "api_secret";

static WebServer* s_server = nullptr;
static bool       s_started = false;

/* ── Auth helper ─────────────────────────────────────────────────────────── */

static bool check_auth() {
    String stored = nvs_store::get_str(NS_API, KEY_SECRET, "");
    if (stored.isEmpty()) {
        /* No secret configured — accept but warn */
        LOG_W("api_srv", "no api_secret set — accepting request without auth");
        return true;
    }
    String hdr = s_server->header("X-EspScreen-Secret");
    if (hdr != stored) {
        LOG_W("api_srv", "auth failed — bad X-EspScreen-Secret");
        return false;
    }
    return true;
}

/* ── CORS / common headers ───────────────────────────────────────────────── */

static void send_json(int code, const String& body) {
    s_server->sendHeader("Access-Control-Allow-Origin", "*");
    s_server->send(code, "application/json", body);
}

/* ── GET /api/health ─────────────────────────────────────────────────────── */

static void handle_health() {
    JsonDocument doc;
    doc["ok"]      = true;
    doc["device"]  = "EspScreen";
    doc["version"] = ESPSCREEN_VERSION;
    doc["uptime_s"] = (int64_t)(millis() / 1000);

    /* WiFi */
    doc["wifi"]["connected"] = wifi_profiles::is_connected();
    if (wifi_profiles::is_connected()) {
        doc["wifi"]["ssid"] = wifi_profiles::get_ssid();
        doc["wifi"]["ip"]   = wifi_profiles::get_ip();
        doc["wifi"]["rssi"]  = wifi_profiles::get_rssi();
    }

    /* Active Claude profile */
    uint8_t count = claude_auth::profile_count();
    doc["claude"]["profile_count"] = count;
    if (count > 0) {
        doc["claude"]["active_label"]  = claude_auth::get_active_label();
        doc["claude"]["token_expired"] = claude_auth::is_token_expired();

        /* expiry */
        uint8_t idx = claude_auth::active_index();
        claude_auth::Profile p;
        if (claude_auth::load_profile(idx, p) && p.expires_at_ms > 0) {
            int64_t now_ms = (int64_t)time(nullptr) * 1000LL;
            int64_t remaining_ms = p.expires_at_ms - now_ms;
            doc["claude"]["expires_in_s"] = (int64_t)(remaining_ms / 1000LL);
        }
    }

    /* API secret status */
    String stored_secret = nvs_store::get_str(NS_API, KEY_SECRET, "");
    doc["api"]["secret_set"] = !stored_secret.isEmpty();
    doc["api"]["port"] = PORT;

    String out;
    serializeJson(doc, out);
    send_json(200, out);
    LOG_I("api_srv", "GET /api/health → 200");
}

/* ── POST /api/claude/tokens ─────────────────────────────────────────────── */

static void handle_tokens() {
    if (!check_auth()) {
        send_json(401, "{\"error\":\"unauthorized\"}");
        return;
    }

    String body = s_server->arg("plain");
    if (body.isEmpty()) {
        send_json(400, "{\"error\":\"empty body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        String errMsg = "{\"error\":\"json parse error: ";
        errMsg += err.c_str();
        errMsg += "\"}";
        send_json(400, errMsg);
        return;
    }

    const char* label      = doc["label"]      | (const char*)nullptr;
    const char* access     = doc["access"]     | (const char*)nullptr;
    const char* refresh_tk = doc["refresh"]    | (const char*)nullptr;
    int64_t     expires_at = doc["expires_at"] | (int64_t)0;

    if (!label || strlen(label) == 0) {
        send_json(400, "{\"error\":\"missing label\"}");
        return;
    }
    if (!access || strlen(access) < 10) {
        send_json(400, "{\"error\":\"missing or invalid access token\"}");
        return;
    }
    if (!refresh_tk || strlen(refresh_tk) < 10) {
        send_json(400, "{\"error\":\"missing or invalid refresh token\"}");
        return;
    }

    if (!claude_auth::set_tokens_by_label(label, access, refresh_tk, expires_at)) {
        String errMsg = "{\"error\":\"profile '";
        errMsg += label;
        errMsg += "' not found or token store failed\"}";
        send_json(404, errMsg);
        LOG_W("api_srv", "POST /api/claude/tokens → 404 label='%s'", label);
        return;
    }

    send_json(200, "{\"ok\":true}");
    LOG_I("api_srv", "POST /api/claude/tokens → 200 label='%s'", label);
}

/* ── OPTIONS preflight (CORS) ────────────────────────────────────────────── */

static void handle_options() {
    s_server->sendHeader("Access-Control-Allow-Origin", "*");
    s_server->sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    s_server->sendHeader("Access-Control-Allow-Headers", "Content-Type, X-EspScreen-Secret");
    s_server->send(204, "text/plain", "");
}

/* ── 404 handler ─────────────────────────────────────────────────────────── */

static void handle_not_found() {
    send_json(404, "{\"error\":\"not found\"}");
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void begin() {
    if (s_started) return;

    s_server = new WebServer(PORT);

    /* Collect the auth header so check_auth() can read it */
    const char* hdrs[] = { "X-EspScreen-Secret" };
    s_server->collectHeaders(hdrs, 1);

    s_server->on("/api/health",         HTTP_GET,     handle_health);
    s_server->on("/api/health",         HTTP_OPTIONS, handle_options);
    s_server->on("/api/claude/tokens",  HTTP_POST,    handle_tokens);
    s_server->on("/api/claude/tokens",  HTTP_OPTIONS, handle_options);
    s_server->onNotFound(handle_not_found);

    s_server->begin();
    s_started = true;
    LOG_I("api_srv", "HTTP API server listening on :%d", PORT);
    Serial.printf("[api_server] listening on :%d\n", PORT);
}

void handle() {
    if (s_server && s_started) {
        s_server->handleClient();
    }
}

void set_secret(const char* secret) {
    if (!secret || strlen(secret) == 0) {
        nvs_store::put_str(NS_API, KEY_SECRET, "");
        LOG_I("api_srv", "api_secret cleared");
    } else {
        nvs_store::put_str(NS_API, KEY_SECRET, secret);
        LOG_I("api_srv", "api_secret updated");
    }
}

String status_str() {
    String s = "port=";
    s += PORT;
    s += "  started=";
    s += s_started ? "yes" : "no";
    String stored_secret = nvs_store::get_str(NS_API, KEY_SECRET, "");
    s += "  secret_set=";
    s += stored_secret.isEmpty() ? "no" : "yes";
    return s;
}

} // namespace api_server

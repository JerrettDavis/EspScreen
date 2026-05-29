/**
 * web_portal.cpp — Browser-based configuration portal on port 80.
 *
 * Two WebServer instances coexist: api_server on :8080, this one on :80.
 * Both are pumped each loop() — Arduino-ESP32's WebServer is single-threaded
 * and non-blocking; handleClient() returns immediately when nothing is pending.
 *
 * Captive portal probe handling (only meaningful in ap_mode=true):
 *   Android: GET /generate_204            → 302 to http://192.168.4.1/
 *   iOS/macOS: GET /hotspot-detect.html   → 302 to http://192.168.4.1/
 *   Windows: GET /connecttest.txt         → 302 to http://192.168.4.1/
 *            GET /ncsi.txt                → 302 to http://192.168.4.1/
 *   Fallback: onNotFound                  → 302 to http://192.168.4.1/
 */

#include "web_portal.h"
#include "portal_assets.h"
#include "net_manager.h"
#include "wifi_profiles.h"
#include "claude_auth.h"
#include "nvs_store.h"
#include "logger.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <time.h>

namespace web_portal {

static const int PORT = 80;
static const char* AP_REDIRECT = "http://192.168.4.1/";

static WebServer* s_server  = nullptr;
static bool       s_active  = false;
static bool       s_ap_mode = false;

/* ── CORS / send helpers ─────────────────────────────────────────────────── */

static void send_json(int code, const String& body) {
    s_server->sendHeader("Access-Control-Allow-Origin", "*");
    s_server->send(code, "application/json", body);
}

static void send_captive_redirect() {
    s_server->sendHeader("Location", AP_REDIRECT, true);
    s_server->send(302, "text/plain", "");
}

/* ── Auth guard for sensitive POST routes ────────────────────────────────── */
/* FIX 2: In AP mode the portal is open by design (first-run provisioning).
 * In STA mode, if an api_secret is configured in NVS we require the same
 * X-EspScreen-Secret header that api_server uses. If no secret is configured
 * we allow the request (matches api_server's lenient behaviour).
 * Reuses the exact same NVS namespace/key as api_server ("espscreen"/"api_secret"). */
static const char* NS_API     = "espscreen";
static const char* KEY_SECRET = "api_secret";

static bool portal_authorized() {
    if (s_ap_mode) return true;   // first-run AP: open by design
    String configured = nvs_store::get_str(NS_API, KEY_SECRET, "");
    if (configured.length() == 0) return true;  // no secret set → allow (lenient, matches api_server)
    return s_server->header("X-EspScreen-Secret") == configured;
}

/* ── GET / ───────────────────────────────────────────────────────────────── */

static void handle_root() {
    s_server->sendHeader("Cache-Control", "no-cache, no-store");
    s_server->send_P(200, "text/html", PORTAL_HTML);
    LOG_I("portal", "GET / → 200 (%u bytes)", (unsigned)sizeof(PORTAL_HTML));
}

/* ── GET /api/scan ───────────────────────────────────────────────────────── */

static void handle_scan() {
    static const uint8_t MAX_SCAN = 12;
    wifi_profiles::ScanResult results[MAX_SCAN];
    uint8_t count = wifi_profiles::scan(results, MAX_SCAN);

    /* Build JSON array without heap-allocating a full JsonDocument for the array;
     * use streaming to stay lean on no-PSRAM device. */
    String out;
    out.reserve(count * 60 + 4);
    out = '[';
    for (uint8_t i = 0; i < count; i++) {
        if (i > 0) out += ',';
        out += "{\"ssid\":\"";
        /* Escape the SSID — most common case is plain ASCII but be safe */
        const char* s = results[i].ssid;
        for (; *s; s++) {
            if (*s == '"' || *s == '\\') out += '\\';
            out += *s;
        }
        out += "\",\"rssi\":";
        out += results[i].rssi;
        out += ",\"enc\":";
        out += results[i].enc;
        out += '}';
    }
    out += ']';

    send_json(200, out);
    LOG_I("portal", "GET /api/scan → %u nets", (unsigned)count);
}

/* ── POST /api/wifi ──────────────────────────────────────────────────────── */

static void handle_wifi_post() {
    if (!portal_authorized()) {
        send_json(401, "{\"error\":\"unauthorized\"}");
        return;
    }
    String body = s_server->arg("plain");
    if (body.length() > 1024) { send_json(413, "{\"error\":\"body too large\"}"); return; }
    if (body.isEmpty()) {
        send_json(400, "{\"error\":\"empty body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        send_json(400, "{\"error\":\"json parse\"}");
        return;
    }

    const char* ssid = doc["ssid"] | (const char*)nullptr;
    const char* pass = doc["pass"] | "";

    if (!ssid || strlen(ssid) == 0) {
        send_json(400, "{\"error\":\"missing ssid\"}");
        return;
    }

    /* Log only the first 20 chars of SSID — never the full list or password */
    LOG_I("portal", "POST /api/wifi ssid=%.20s", ssid);

    if (s_ap_mode) {
        /* AP mode: trigger state machine retry; response is optimistic — the
         * client browser is still on the AP so it will see the status update. */
        wifi_profiles::add_network(ssid, pass);
        net_manager::_trigger_sta_retry(ssid, pass);

        String out = "{\"ok\":true,\"ip\":\"";
        out += net_manager::ip_string();
        out += "\"}";
        send_json(200, out);
    } else {
        /* STA mode: try to connect immediately */
        bool ok = wifi_profiles::connect_now(ssid, pass, 12000);
        if (ok) {
            String out = "{\"ok\":true,\"ip\":\"";
            out += wifi_profiles::get_ip();
            out += "\"}";
            send_json(200, out);
            LOG_I("portal", "POST /api/wifi → connected ip=%s", wifi_profiles::get_ip().c_str());
        } else {
            send_json(200, "{\"ok\":false,\"error\":\"connection failed\"}");
            LOG_W("portal", "POST /api/wifi → connect failed");
        }
    }
}

/* ── GET /api/status ─────────────────────────────────────────────────────── */

static void handle_status() {
    JsonDocument doc;
    doc["ok"]       = true;
    doc["device"]   = "EspScreen";
    doc["version"]  = ESPSCREEN_VERSION;
    doc["uptime_s"] = (int64_t)(millis() / 1000);

    /* WiFi — reuse same shape as api_server /api/health */
    doc["wifi"]["connected"] = wifi_profiles::is_connected();
    if (wifi_profiles::is_connected()) {
        doc["wifi"]["ssid"] = wifi_profiles::get_ssid();
        doc["wifi"]["ip"]   = wifi_profiles::get_ip();
        doc["wifi"]["rssi"] = wifi_profiles::get_rssi();
    }

    /* net_manager state */
    const char* mode_str = "unknown";
    switch (net_manager::mode()) {
        case net_manager::Mode::Boot:          mode_str = "Boot";          break;
        case net_manager::Mode::StaConnecting: mode_str = "StaConnecting"; break;
        case net_manager::Mode::StaConnected:  mode_str = "StaConnected";  break;
        case net_manager::Mode::ApPortal:      mode_str = "ApPortal";      break;
        case net_manager::Mode::ApStaRetry:    mode_str = "ApStaRetry";    break;
    }
    doc["net_mode"]  = mode_str;
    doc["portal_ip"] = net_manager::ip_string();
    doc["last_retry_failed"] = net_manager::last_retry_failed();

    /* Active Claude profile — same shape as api_server /api/health */
    uint8_t count = claude_auth::profile_count();
    doc["claude"]["profile_count"] = count;
    if (count > 0) {
        doc["claude"]["active_label"]  = claude_auth::get_active_label();
        doc["claude"]["token_expired"] = claude_auth::is_token_expired();

        uint8_t idx = claude_auth::active_index();
        claude_auth::Profile p;
        if (claude_auth::load_profile(idx, p) && p.expires_at_ms > 0) {
            int64_t now_ms = (int64_t)time(nullptr) * 1000LL;
            int64_t remaining_ms = p.expires_at_ms - now_ms;
            doc["claude"]["expires_in_s"] = (int64_t)(remaining_ms / 1000LL);
        }
    }

    String out;
    serializeJson(doc, out);
    send_json(200, out);
    LOG_I("portal", "GET /api/status → 200");
}

/* ── POST /api/claude/profile ────────────────────────────────────────────── */

static void handle_claude_profile() {
    if (!portal_authorized()) {
        send_json(401, "{\"error\":\"unauthorized\"}");
        return;
    }
    String body = s_server->arg("plain");
    if (body.length() > 1024) { send_json(413, "{\"error\":\"body too large\"}"); return; }
    if (body.isEmpty()) {
        send_json(400, "{\"error\":\"empty body\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        send_json(400, "{\"error\":\"json parse\"}");
        return;
    }

    const char* label = doc["label"] | (const char*)nullptr;
    if (!label || strlen(label) == 0) {
        send_json(400, "{\"error\":\"missing label\"}");
        return;
    }

    /* Try to find existing; if not found, add it */
    uint8_t idx = claude_auth::find_by_label(label);
    if (idx == 255) {
        idx = claude_auth::add_profile(label);
        if (idx == 255) {
            send_json(500, "{\"error\":\"could not add profile (max reached or duplicate)\"}");
            LOG_W("portal", "POST /api/claude/profile add failed label=%.20s", label);
            return;
        }
        LOG_I("portal", "POST /api/claude/profile added idx=%u", (unsigned)idx);
    }

    /* Select it as active */
    claude_auth::set_active(idx);

    String out = "{\"ok\":true,\"index\":";
    out += idx;
    out += '}';
    send_json(200, out);
    LOG_I("portal", "POST /api/claude/profile → ok idx=%u", (unsigned)idx);
}

/* ── POST /api/claude/tokens ─────────────────────────────────────────────── */

static void handle_claude_tokens() {
    if (!portal_authorized()) {
        send_json(401, "{\"error\":\"unauthorized\"}");
        return;
    }
    String body = s_server->arg("plain");
    if (body.length() > 2048) { send_json(413, "{\"error\":\"body too large\"}"); return; }
    if (body.isEmpty()) {
        send_json(400, "{\"error\":\"empty body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        String errMsg = "{\"error\":\"json parse: ";
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

    /* Use the SAME function api_server uses — zero new token logic */
    if (!claude_auth::set_tokens_by_label(label, access, refresh_tk, expires_at)) {
        String errMsg = "{\"error\":\"profile '";
        errMsg += label;
        errMsg += "' not found or token store failed\"}";
        send_json(404, errMsg);
        LOG_W("portal", "POST /api/claude/tokens 404 label=%.20s", label);
        return;
    }

    send_json(200, "{\"ok\":true}");
    LOG_I("portal", "POST /api/claude/tokens ok label=%.20s", label);
}

/* ── OPTIONS preflight (CORS) ────────────────────────────────────────────── */

static void handle_options() {
    s_server->sendHeader("Access-Control-Allow-Origin", "*");
    s_server->sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    s_server->sendHeader("Access-Control-Allow-Headers", "Content-Type, X-EspScreen-Secret");
    s_server->send(204, "text/plain", "");
}

/* ── Captive portal catch-all probes ─────────────────────────────────────── */

static void handle_not_found() {
    if (s_ap_mode) {
        send_captive_redirect();
    } else {
        send_json(404, "{\"error\":\"not found\"}");
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void begin(bool ap_mode) {
    if (s_active) {
        /* If already active but mode changes (e.g. STA→AP forced), restart */
        if (s_ap_mode == ap_mode) return;
        end();
    }

    s_ap_mode = ap_mode;
    s_server  = new WebServer(PORT);

    /* FIX 2: Register the auth header so s_server->header() can read it in
     * portal_authorized(). Mirrors what api_server does in its begin(). */
    const char* hdrs[] = { "X-EspScreen-Secret" };
    s_server->collectHeaders(hdrs, 1);

    /* Root SPA */
    s_server->on("/",                      HTTP_GET,     handle_root);
    s_server->on("/",                      HTTP_OPTIONS, handle_options);

    /* WiFi scan + connect */
    s_server->on("/api/scan",              HTTP_GET,     handle_scan);
    s_server->on("/api/wifi",              HTTP_POST,    handle_wifi_post);
    s_server->on("/api/wifi",              HTTP_OPTIONS, handle_options);

    /* Status */
    s_server->on("/api/status",            HTTP_GET,     handle_status);
    s_server->on("/api/status",            HTTP_OPTIONS, handle_options);

    /* Claude profile management */
    s_server->on("/api/claude/profile",    HTTP_POST,    handle_claude_profile);
    s_server->on("/api/claude/profile",    HTTP_OPTIONS, handle_options);
    s_server->on("/api/claude/tokens",     HTTP_POST,    handle_claude_tokens);
    s_server->on("/api/claude/tokens",     HTTP_OPTIONS, handle_options);

    /* Captive portal probe URLs — only redirect in AP mode, but register always
     * so that end() and begin(false) don't leave stale handlers */
    if (ap_mode) {
        s_server->on("/generate_204",          HTTP_GET, [](){ send_captive_redirect(); });
        s_server->on("/hotspot-detect.html",   HTTP_GET, [](){ send_captive_redirect(); });
        s_server->on("/connecttest.txt",       HTTP_GET, [](){ send_captive_redirect(); });
        s_server->on("/ncsi.txt",              HTTP_GET, [](){ send_captive_redirect(); });
    }

    s_server->onNotFound(handle_not_found);
    s_server->begin();
    s_active = true;

    LOG_I("portal", "web portal :%d started ap_mode=%d heap=%lu",
          PORT, (int)ap_mode, (unsigned long)esp_get_free_heap_size());
    Serial.printf("[web_portal] listening on :%d  ap_mode=%s\n",
                  PORT, ap_mode ? "true" : "false");
}

void handle() {
    if (s_server && s_active) {
        s_server->handleClient();
    }
}

void end() {
    if (s_server) {
        s_server->stop();
        delete s_server;
        s_server = nullptr;
    }
    s_active  = false;
    s_ap_mode = false;
    LOG_I("portal", "web portal stopped");
}

bool active() {
    return s_active;
}

} // namespace web_portal

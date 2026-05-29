/**
 * net_manager.cpp — WiFi + portal lifecycle state machine.
 *
 * Owns: wifi_profiles::init(), api_server::begin(), web_portal::begin/end.
 * Does NOT touch claude_auth or nvs_store directly.
 */

#include "net_manager.h"
#include "wifi_profiles.h"
#include "api_server.h"
#include "web_portal.h"
#include "logger.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <Arduino.h>

namespace net_manager {

/* ── Constants ──────────────────────────────────────────────────────────────── */

static const char*    AP_SSID          = "EspScreen-Setup";
static const uint8_t  DNS_PORT         = 53;
static const IPAddress AP_IP(192, 168, 4, 1);
static const uint32_t STA_LINK_DEBOUNCE_MS = 30000UL;  // 30 s before declaring link lost
static const uint32_t AP_GRACE_MS          = 3000UL;   // grace period after ApStaRetry success
static const uint32_t RETRY_INTERVAL_MS    = 20000UL;  // how long to wait before retry in StaConnecting

/* ── State ──────────────────────────────────────────────────────────────────── */

static Mode      s_mode            = Mode::Boot;
static bool      s_api_started     = false;
static DNSServer s_dns;
static bool      s_dns_started     = false;

/* Reconnect hysteresis */
static uint32_t  s_lost_at_ms      = 0;   // millis() when link was first seen down
static bool      s_link_was_up     = false;

/* FIX 3: module-level so enter_sta_connecting() can reset it on re-entry,
 * preventing a stale timer from a previous StaConnecting visit. */
static uint32_t  s_conn_wait_start = 0;

/* ApStaRetry state */
static char      s_retry_ssid[65]  = {};
static char      s_retry_pass[65]  = {};
static bool      s_retry_pending   = false;
static uint32_t  s_retry_grace_end = 0;

/* Error flag forwarded to web_portal /api/status */
static bool      s_last_retry_failed = false;

/* ── Forward declarations ───────────────────────────────────────────────────── */
static void enter_ap_portal();
static void enter_sta_connected();
static void enter_sta_connecting();

/* ── Helpers ────────────────────────────────────────────────────────────────── */

static void stop_dns() {
    if (s_dns_started) {
        s_dns.stop();
        s_dns_started = false;
    }
}

/* ── State entry functions ──────────────────────────────────────────────────── */

static void enter_sta_connecting() {
    LOG_I("net_mgr", "→ StaConnecting");
    s_mode = Mode::StaConnecting;
    s_link_was_up      = false;
    s_lost_at_ms       = 0;
    s_conn_wait_start  = 0;   // FIX 3: reset on every entry to avoid stale timer
    stop_dns();

    /* Attempt connection via existing priority logic */
    wifi_profiles::init();

    if (wifi_profiles::is_connected()) {
        enter_sta_connected();
    } else if (wifi_profiles::network_count() == 0) {
        LOG_I("net_mgr", "no networks stored → ApPortal");
        enter_ap_portal();
    } else {
        /* No immediate connection; stay in StaConnecting.
         * loop() will watch for is_connected() or timeout → ApPortal. */
        LOG_I("net_mgr", "StaConnecting — waiting for association");
    }
}

static void enter_sta_connected() {
    LOG_I("net_mgr", "→ StaConnected  ip=%s", WiFi.localIP().toString().c_str());
    s_mode         = Mode::StaConnected;
    s_link_was_up  = true;
    s_lost_at_ms   = 0;
    stop_dns();

    /* Start api_server once — idempotent (it guards internally) */
    if (!s_api_started) {
        api_server::begin();
        s_api_started = true;
    }

    /* Start web portal on STA IP (not AP mode) */
    web_portal::begin(false);
    LOG_I("net_mgr", "free heap after web_portal::begin=%lu",
          (unsigned long)esp_get_free_heap_size());
}

static void enter_ap_portal() {
    LOG_I("net_mgr", "→ ApPortal");
    s_mode             = Mode::ApPortal;
    s_last_retry_failed = false;

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID);
    delay(100); // let softAP settle before starting DNS

    /* Start captive DNS — all names resolve to the AP IP */
    s_dns.start(DNS_PORT, "*", AP_IP);
    s_dns_started = true;

    web_portal::begin(true);
    LOG_I("net_mgr", "AP up  ssid=%s  ip=%s  heap=%lu",
          AP_SSID, AP_IP.toString().c_str(),
          (unsigned long)esp_get_free_heap_size());
    Serial.printf("[net_mgr] Captive portal: connect to '%s' and open http://%s\n",
                  AP_SSID, AP_IP.toString().c_str());
}

/* ── Public retry trigger (called by web_portal on POST /api/wifi) ─────────── */

/* Package name forward-declaration so web_portal can call back here */
void _trigger_sta_retry(const char* ssid, const char* pass) {
    strlcpy(s_retry_ssid, ssid, sizeof(s_retry_ssid));
    strlcpy(s_retry_pass, pass, sizeof(s_retry_pass));
    s_retry_pending   = true;
    s_mode            = Mode::ApStaRetry;
    s_retry_grace_end = 0;
    LOG_I("net_mgr", "ApStaRetry triggered ssid=%.20s", ssid);
}

/* ── Public API ─────────────────────────────────────────────────────────────── */

void init() {
    WiFi.setAutoReconnect(true);
    enter_sta_connecting();
}

void loop() {
    /* Always pump DNS when AP is up */
    if (s_dns_started) {
        s_dns.processNextRequest();
    }

    /* Always pump web portal */
    web_portal::handle();

    switch (s_mode) {

    case Mode::Boot:
        /* Should not remain here; init() always transitions away */
        enter_sta_connecting();
        break;

    case Mode::StaConnecting: {
        /* Poll for connection; fall to AP if still nothing after init() */
        if (wifi_profiles::is_connected()) {
            enter_sta_connected();
        } else {
            /* wifi_profiles::init() is blocking, so if we're still here
             * the networks were tried and none connected.  Go to portal. */
            if (wifi_profiles::network_count() == 0) {
                enter_ap_portal();
            } else {
                /* Give a brief window for background reconnect, then portal.
                 * s_conn_wait_start is a module-level static (FIX 3) reset by
                 * enter_sta_connecting() so it is always fresh on re-entry. */
                if (s_conn_wait_start == 0) s_conn_wait_start = millis();
                if ((millis() - s_conn_wait_start) > RETRY_INTERVAL_MS) {
                    s_conn_wait_start = 0;
                    LOG_I("net_mgr", "connection timeout → ApPortal");
                    enter_ap_portal();
                }
            }
        }
        break;
    }

    case Mode::StaConnected: {
        /* Hysteresis: debounce transient link drops before declaring lost */
        bool connected = wifi_profiles::is_connected();
        if (connected) {
            s_link_was_up = true;
            s_lost_at_ms  = 0;
        } else {
            if (s_lost_at_ms == 0) {
                s_lost_at_ms = millis();
                LOG_W("net_mgr", "WiFi link lost — debouncing %lu ms", STA_LINK_DEBOUNCE_MS);
            } else if ((millis() - s_lost_at_ms) > STA_LINK_DEBOUNCE_MS) {
                LOG_W("net_mgr", "link down >30s — re-entering StaConnecting");
                web_portal::end();
                enter_sta_connecting();
            }
        }
        break;
    }

    case Mode::ApPortal:
        /* Waiting for user to submit /api/wifi → _trigger_sta_retry() */
        break;

    case Mode::ApStaRetry: {
        if (s_retry_pending) {
            s_retry_pending = false;

            /* Add the new network and attempt immediate connection */
            wifi_profiles::add_network(s_retry_ssid, s_retry_pass);
            bool ok = wifi_profiles::connect_now(s_retry_ssid, s_retry_pass, 12000);

            if (ok) {
                s_last_retry_failed = false;
                LOG_I("net_mgr", "ApStaRetry success — grace period %lu ms", AP_GRACE_MS);
                s_retry_grace_end = millis() + AP_GRACE_MS;
            } else {
                s_last_retry_failed = true;
                LOG_W("net_mgr", "ApStaRetry failed — back to ApPortal");
                enter_ap_portal();
            }
        } else if (s_retry_grace_end != 0 && millis() >= s_retry_grace_end) {
            /* Grace period expired — tear down AP, switch to pure STA */
            s_retry_grace_end = 0;
            web_portal::end();
            stop_dns();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            LOG_I("net_mgr", "AP torn down → StaConnected");
            enter_sta_connected();
        }
        break;
    }

    } // switch
}

Mode mode() {
    return s_mode;
}

const char* ap_ssid() {
    return AP_SSID;
}

String ip_string() {
    if (s_mode == Mode::ApPortal || s_mode == Mode::ApStaRetry) {
        return AP_IP.toString();
    }
    if (wifi_profiles::is_connected()) {
        return wifi_profiles::get_ip();
    }
    return String("0.0.0.0");
}

void force_portal() {
    LOG_I("net_mgr", "force_portal() called");
    if (s_mode == Mode::StaConnected) {
        /* Don't tear down STA — use AP+STA so the M2M API stays up */
        s_mode = Mode::ApPortal;
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(AP_SSID);
        delay(100);
        s_dns.start(DNS_PORT, "*", AP_IP);
        s_dns_started = true;
        web_portal::begin(true);
        LOG_I("net_mgr", "force portal — AP+STA  heap=%lu",
              (unsigned long)esp_get_free_heap_size());
    } else {
        enter_ap_portal();
    }
}

/* Expose the retry-failed flag for web_portal /api/status */
bool last_retry_failed() {
    return s_last_retry_failed;
}

} // namespace net_manager

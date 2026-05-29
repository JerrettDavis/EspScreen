/**
 * claude_widget.cpp — Live Claude usage dashboard for EspScreen.
 *
 * Polls GET https://api.anthropic.com/api/oauth/usage directly via HTTPS.
 * Uses active Claude profile's OAuth access token (Bearer + anthropic-beta header).
 * On token expiry, shows "Token expired" badge — refresh not implemented in v1.
 *
 * TODO: pin Anthropic CA certificate (currently using setInsecure() for dev).
 *
 * Color scheme for utilization bars:
 *   0-60%:   green  #4ADE80
 *   60-85%:  amber  #FBBF24
 *   85-100%: red    #EF4444
 */

#include "claude_widget.h"
#include "../../os/screen_router.h"
#include "../../os/nvs_store.h"
#include "../../os/logger.h"
#include "../../os/wifi_profiles.h"
#include "../../os/claude_auth.h"
#include "../../ui/widgets.h"
#include <lvgl.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <time.h>

namespace claude_widget {

/* ── Direct API endpoint (no longer NVS-backed) ──────────────────────── */
static const char* ANTHROPIC_USAGE_URL = "https://api.anthropic.com/api/oauth/usage";
static const char* ANTHROPIC_BETA_HDR  = "oauth-2025-04-20";

/* ── Timer / screen state ─────────────────────────────────────────────── */
static lv_timer_t* s_poll_timer      = nullptr;
static lv_timer_t* s_immediate_timer = nullptr;
static lv_obj_t*   s_screen          = nullptr;

/* ── UI element handles ───────────────────────────────────────────────── */
static lv_obj_t* s_dot_connected    = nullptr;
static lv_obj_t* s_lbl_model        = nullptr;
static lv_obj_t* s_bar_5h           = nullptr;
static lv_obj_t* s_lbl_5h_pct       = nullptr;
static lv_obj_t* s_lbl_5h_reset     = nullptr;
static lv_obj_t* s_bar_7d           = nullptr;
static lv_obj_t* s_lbl_7d_pct       = nullptr;
static lv_obj_t* s_lbl_7d_reset     = nullptr;
static lv_obj_t* s_lbl_cost         = nullptr;
static lv_obj_t* s_lbl_context      = nullptr;
static lv_obj_t* s_lbl_cache_ttl    = nullptr;
static lv_obj_t* s_headroom_cont    = nullptr;
static lv_obj_t* s_lbl_tokens_saved = nullptr;
static lv_obj_t* s_lbl_compression  = nullptr;
static lv_obj_t* s_lbl_updated      = nullptr;
static lv_obj_t* s_badge_stale      = nullptr;
static lv_obj_t* s_badge_expired    = nullptr;  // "Token expired" badge
static lv_obj_t* s_lbl_status       = nullptr;
static lv_obj_t* s_lbl_title        = nullptr;  // top-bar title (shows profile name)

/* ── Helpers ──────────────────────────────────────────────────────────── */

static lv_color_t bar_color(int util) {
    if (util >= 85) return lv_color_hex(0xEF4444);
    if (util >= 60) return lv_color_hex(0xFBBF24);
    return lv_color_hex(0x4ADE80);
}

static void fmt_duration(char* buf, size_t n, int32_t secs) {
    if (secs <= 0) {
        snprintf(buf, n, "now");
    } else if (secs < 3600) {
        snprintf(buf, n, "%dm", (int)(secs / 60));
    } else if (secs < 86400) {
        snprintf(buf, n, "%dh %dm", (int)(secs / 3600), (int)((secs % 3600) / 60));
    } else {
        snprintf(buf, n, "%dd %dh", (int)(secs / 86400), (int)((secs % 86400) / 3600));
    }
}

static void set_bar_value(lv_obj_t* bar, int pct) {
    if (!bar) return;
    pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    lv_bar_set_value(bar, pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar, bar_color(pct), LV_PART_INDICATOR);
}

static void show_stale(bool stale) {
    if (!s_badge_stale) return;
    if (stale) lv_obj_remove_flag(s_badge_stale, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_add_flag(s_badge_stale, LV_OBJ_FLAG_HIDDEN);
}

static void show_expired(bool expired) {
    if (!s_badge_expired) return;
    if (expired) lv_obj_remove_flag(s_badge_expired, LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_add_flag(s_badge_expired, LV_OBJ_FLAG_HIDDEN);
}

static void set_connected(bool ok) {
    if (!s_dot_connected) return;
    lv_obj_set_style_bg_color(s_dot_connected,
        ok ? lv_color_hex(0x4ADE80) : lv_color_hex(0xEF4444), 0);
}

static void set_status(const char* msg) {
    if (s_lbl_status) lv_label_set_text(s_lbl_status, msg);
}

static void update_title() {
    if (!s_lbl_title) return;
    String label = claude_auth::get_active_label();
    String title = "Claude";
    if (label != "(none)") {
        title = "Claude \xe2\x80\x94 " + label;  // UTF-8 em dash
    }
    lv_label_set_text(s_lbl_title, title.c_str());
}

static void stamp_update_time() {
    if (!s_lbl_updated) return;
    time_t now = time(nullptr);
    struct tm* ti = localtime(&now);
    char buf[32];
    if (ti && now > 1000000) {
        snprintf(buf, sizeof(buf), "Updated %02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
    } else {
        snprintf(buf, sizeof(buf), "Updated --:--:--");
    }
    lv_label_set_text(s_lbl_updated, buf);
}

/* ── JSON parsing — maps Anthropic API shape directly ────────────────── */
/*
 * Anthropic GET /api/oauth/usage response shape (confirmed via server.js):
 * {
 *   "five_hour":  { "utilization": 0.65, "resets_at": "2026-05-28T12:30:00Z" },
 *   "seven_day":  { "utilization": 0.12, "resets_at": "2026-06-04T00:00:00Z" },
 *   ...
 * }
 * We map utilization (0..1 float) to 0..100 int for the bars.
 */
static void apply_json(const char* json_str) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_str);
    if (err) {
        LOG_W("claude_w", "JSON parse error: %s", err.c_str());
        set_status(("JSON error: " + String(err.c_str())).c_str());
        set_connected(false);
        return;
    }

    set_connected(true);
    set_status("");
    show_stale(false);

    /* five_hour */
    JsonObject fh = doc["five_hour"];
    if (!fh.isNull()) {
        float util_frac = fh["utilization"] | 0.0f;
        int pct5h = (int)(util_frac * 100.0f + 0.5f);
        const char* resets_at_5h = fh["resets_at"] | nullptr;
        int32_t reset5h = 0;
        if (resets_at_5h) {
            struct tm t = {};
            /* Parse ISO 8601 — only works if time is synced */
            char* ok = strptime(resets_at_5h, "%Y-%m-%dT%H:%M:%SZ", &t);
            if (ok) {
                time_t target = mktime(&t);
                reset5h = (int32_t)(target - time(nullptr));
                if (reset5h < 0) reset5h = 0;
            }
        }
        set_bar_value(s_bar_5h, pct5h);
        char pct_buf[16];
        snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct5h);
        lv_label_set_text(s_lbl_5h_pct, pct_buf);
        char dur_buf[24], reset_buf[40];
        fmt_duration(dur_buf, sizeof(dur_buf), reset5h);
        snprintf(reset_buf, sizeof(reset_buf), "Resets in %s", dur_buf);
        lv_label_set_text(s_lbl_5h_reset, reset_buf);
    }

    /* seven_day */
    JsonObject sd = doc["seven_day"];
    if (!sd.isNull()) {
        float util_frac = sd["utilization"] | 0.0f;
        int pct7d = (int)(util_frac * 100.0f + 0.5f);
        const char* resets_at_7d = sd["resets_at"] | nullptr;
        int32_t reset7d = 0;
        if (resets_at_7d) {
            struct tm t = {};
            char* ok = strptime(resets_at_7d, "%Y-%m-%dT%H:%M:%SZ", &t);
            if (ok) {
                time_t target = mktime(&t);
                reset7d = (int32_t)(target - time(nullptr));
                if (reset7d < 0) reset7d = 0;
            }
        }
        set_bar_value(s_bar_7d, pct7d);
        char pct_buf[16];
        snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct7d);
        lv_label_set_text(s_lbl_7d_pct, pct_buf);
        char dur_buf[24], reset_buf[40];
        fmt_duration(dur_buf, sizeof(dur_buf), reset7d);
        snprintf(reset_buf, sizeof(reset_buf), "Resets in %s", dur_buf);
        lv_label_set_text(s_lbl_7d_reset, reset_buf);
    }

    /* Session fields — not present in the usage API, show placeholders */
    lv_label_set_text(s_lbl_model, "Claude");
    lv_label_set_text(s_lbl_cost, "---");
    lv_label_set_text(s_lbl_context, "---");
    lv_label_set_text(s_lbl_cache_ttl, "---");

    /* Headroom — not available in direct-API mode */
    if (s_headroom_cont) lv_obj_add_flag(s_headroom_cont, LV_OBJ_FLAG_HIDDEN);

    stamp_update_time();
}

/* ── HTTPS poll ───────────────────────────────────────────────────────── */

void poll_now() {
    if (!wifi_profiles::is_connected()) {
        set_connected(false);
        set_status("WiFi not connected");
        LOG_W("claude_w", "poll_now: WiFi not connected");
        return;
    }

    /* Get access token for active profile */
    bool is_expired = false;
    String token = claude_auth::get_active_access_token(&is_expired);

    if (token.isEmpty()) {
        if (claude_auth::profile_count() == 0) {
            set_connected(false);
            set_status("No Claude profile configured");
            LOG_W("claude_w", "poll_now: no profiles configured");
        } else {
            set_connected(false);
            set_status("No access token configured");
            show_expired(true);
            LOG_W("claude_w", "poll_now: access token is empty");
        }
        return;
    }

    /* Show expired badge if token is past expiry — but still attempt the call */
    show_expired(is_expired);
    if (is_expired) {
        LOG_W("claude_w", "poll_now: token is expired — attempting anyway (v1: no refresh)");
    }

    /* Log heap before HTTPS handshake */
    LOG_I("claude_w", "pre-HTTPS heap: %lu", (unsigned long)esp_get_free_heap_size());

    /* HTTPS with insecure cert (TODO: pin Anthropic CA) */
    WiFiClientSecure client;
    client.setInsecure();  // TODO: pin cert for production

    HTTPClient http;
    http.begin(client, ANTHROPIC_USAGE_URL);
    http.setTimeout(10000);
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("anthropic-beta", ANTHROPIC_BETA_HDR);
    http.addHeader("Content-Type", "application/json");

    int code = http.GET();

    LOG_I("claude_w", "post-HTTPS heap: %lu  HTTP code=%d",
          (unsigned long)esp_get_free_heap_size(), code);

    if (code == 200) {
        String body = http.getString();
        http.end();
        client.stop();
        LOG_I("claude_w", "Poll OK (%d bytes)", body.length());
        show_expired(false);  // successful call — clear expired badge
        apply_json(body.c_str());
    } else if (code == 401) {
        http.end();
        client.stop();
        /* 401 = token expired/invalid. v1: show badge, no refresh. */
        set_connected(false);
        show_expired(true);
        set_status("Token expired — use 'claude token set'");
        LOG_W("claude_w", "poll_now: 401 Unauthorized — token expired");
    } else {
        String body = http.getString();
        http.end();
        client.stop();
        char err_buf[64];
        snprintf(err_buf, sizeof(err_buf), "HTTP error: %d", code);
        LOG_W("claude_w", "%s  body=%s", err_buf, body.substring(0, 80).c_str());
        set_status(err_buf);
        set_connected(false);
    }

    /* Update title to show current profile */
    update_title();
}

/* ── Timer callbacks ──────────────────────────────────────────────────── */

static void poll_cb(lv_timer_t* /*t*/) {
    if (s_screen && lv_scr_act() == s_screen) poll_now();
}

static void immediate_poll_cb(lv_timer_t* t) {
    lv_timer_delete(t);
    s_immediate_timer = nullptr;
    if (s_screen && lv_scr_act() == s_screen) poll_now();
}

static void back_cb(lv_event_t* /*e*/) { screen_router::pop(); }
static void refresh_btn_cb(lv_event_t* /*e*/) { poll_now(); }

/* ── Layout helpers ───────────────────────────────────────────────────── */

static lv_obj_t* add_divider(lv_obj_t* parent, lv_coord_t y) {
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_set_size(line, 290, 1);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(line, lv_color_hex(0x444466), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    return line;
}

static lv_obj_t* add_kv_row(lv_obj_t* parent, const char* key_text,
                              lv_coord_t y, lv_obj_t** val_label_out) {
    lv_obj_t* key = lv_label_create(parent);
    lv_label_set_text(key, key_text);
    lv_obj_set_style_text_font(key, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(key, lv_color_hex(0xaaaacc), 0);
    lv_obj_align(key, LV_ALIGN_TOP_LEFT, 15, y);

    lv_obj_t* val = lv_label_create(parent);
    lv_label_set_text(val, "---");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(val, lv_color_hex(0xffffff), 0);
    lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -15, y);
    *val_label_out = val;
    return key;
}

static void add_section_label(lv_obj_t* parent, const char* text, lv_coord_t y) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x8888aa), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);
}

static lv_obj_t* add_bar_row(lv_obj_t* parent, const char* key_text, lv_coord_t y,
                               lv_obj_t** pct_label_out, lv_obj_t** reset_label_out) {
    lv_obj_t* key = lv_label_create(parent);
    lv_label_set_text(key, key_text);
    lv_obj_set_style_text_font(key, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(key, lv_color_hex(0xaaaacc), 0);
    lv_obj_align(key, LV_ALIGN_TOP_LEFT, 15, y);

    lv_obj_t* pct = lv_label_create(parent);
    lv_label_set_text(pct, "0%");
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pct, lv_color_hex(0xffffff), 0);
    lv_obj_align(pct, LV_ALIGN_TOP_RIGHT, -15, y);
    *pct_label_out = pct;

    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 290, 14);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, y + 18);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x4ADE80), LV_PART_INDICATOR);

    lv_obj_t* reset = lv_label_create(parent);
    lv_label_set_text(reset, "Resets in --");
    lv_obj_set_style_text_font(reset, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(reset, lv_color_hex(0x888899), 0);
    lv_obj_align(reset, LV_ALIGN_TOP_LEFT, 15, y + 36);
    *reset_label_out = reset;

    return bar;
}

/* ── Screen builder ───────────────────────────────────────────────────── */

lv_obj_t* create_screen() {
    if (s_poll_timer) { lv_timer_delete(s_poll_timer); s_poll_timer = nullptr; }
    if (s_immediate_timer) { lv_timer_delete(s_immediate_timer); s_immediate_timer = nullptr; }
    s_screen = nullptr;

    /* Check for legacy endpoint config and warn */
    String legacy_ep = nvs_store::get_str("claude", "endpoint", "");
    if (!legacy_ep.isEmpty()) {
        LOG_I("claude_w", "Legacy 'endpoint' config ignored — using direct OAuth path");
    }

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d0d1a), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    s_screen = scr;

    /* ── Top bar ──────────────────────────────────────────────────────── */
    lv_obj_t* topbar = lv_obj_create(scr);
    lv_obj_set_size(topbar, LV_HOR_RES, 36);
    lv_obj_align(topbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_pad_all(topbar, 0, 0);
    lv_obj_set_style_radius(topbar, 0, 0);

    lv_obj_t* back_btn = lv_button_create(topbar);
    lv_obj_set_size(back_btn, 48, 28);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_radius(back_btn, 6, 0);
    lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    /* Title — shows active profile name */
    s_lbl_title = lv_label_create(topbar);
    lv_label_set_text(s_lbl_title, "Claude Usage");
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    /* Stale badge */
    s_badge_stale = lv_label_create(topbar);
    lv_label_set_text(s_badge_stale, "stale!");
    lv_obj_set_style_text_color(s_badge_stale, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_text_font(s_badge_stale, &lv_font_montserrat_14, 0);
    lv_obj_align(s_badge_stale, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_flag(s_badge_stale, LV_OBJ_FLAG_HIDDEN);

    /* ── Content area ─────────────────────────────────────────────────── */
    lv_coord_t y = 44;

    s_lbl_model = lv_label_create(scr);
    lv_label_set_text(s_lbl_model, "---");
    lv_obj_set_style_text_font(s_lbl_model, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_model, lv_color_hex(0xddddff), 0);
    lv_obj_align(s_lbl_model, LV_ALIGN_TOP_LEFT, 15, y);

    s_dot_connected = lv_obj_create(scr);
    lv_obj_set_size(s_dot_connected, 12, 12);
    lv_obj_align(s_dot_connected, LV_ALIGN_TOP_RIGHT, -15, y + 2);
    lv_obj_set_style_radius(s_dot_connected, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot_connected, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_border_width(s_dot_connected, 0, 0);
    lv_obj_set_style_pad_all(s_dot_connected, 0, 0);

    y += 20;
    add_divider(scr, y);

    /* Token expired badge — below top divider */
    y += 6;
    s_badge_expired = lv_label_create(scr);
    lv_label_set_text(s_badge_expired, "Token expired — use 'claude token set'");
    lv_obj_set_style_text_font(s_badge_expired, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_badge_expired, lv_color_hex(0xEF4444), 0);
    lv_obj_align(s_badge_expired, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_flag(s_badge_expired, LV_OBJ_FLAG_HIDDEN);

    /* Status/error line */
    y += 14;
    s_lbl_status = lv_label_create(scr);
    lv_label_set_text(s_lbl_status, "");
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xEF4444), 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_TOP_MID, 0, y);

    /* 5-hour bar */
    y += 6;
    s_bar_5h = add_bar_row(scr, "5-hour limit", y, &s_lbl_5h_pct, &s_lbl_5h_reset);
    y += 56;

    /* 7-day bar */
    s_bar_7d = add_bar_row(scr, "7-day limit", y, &s_lbl_7d_pct, &s_lbl_7d_reset);
    y += 56;

    /* Session section */
    add_divider(scr, y);
    y += 6;
    add_section_label(scr, "--- This session ---", y);
    y += 18;
    add_kv_row(scr, "Cost", y, &s_lbl_cost);          y += 18;
    add_kv_row(scr, "Context used", y, &s_lbl_context); y += 18;
    add_kv_row(scr, "Cache TTL", y, &s_lbl_cache_ttl);  y += 22;

    /* Headroom section (hidden — not available in direct-API mode) */
    add_divider(scr, y);
    y += 6;
    s_headroom_cont = lv_obj_create(scr);
    lv_obj_set_size(s_headroom_cont, LV_HOR_RES, 58);
    lv_obj_align(s_headroom_cont, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_opa(s_headroom_cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_headroom_cont, 0, 0);
    lv_obj_set_style_pad_all(s_headroom_cont, 0, 0);
    lv_obj_add_flag(s_headroom_cont, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* hr_key1 = lv_label_create(s_headroom_cont);
    lv_label_set_text(hr_key1, "Tokens saved");
    lv_obj_set_style_text_font(hr_key1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hr_key1, lv_color_hex(0xaaaacc), 0);
    lv_obj_align(hr_key1, LV_ALIGN_TOP_LEFT, 15, 18);
    s_lbl_tokens_saved = lv_label_create(s_headroom_cont);
    lv_label_set_text(s_lbl_tokens_saved, "---");
    lv_obj_set_style_text_font(s_lbl_tokens_saved, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_tokens_saved, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_lbl_tokens_saved, LV_ALIGN_TOP_RIGHT, -15, 18);
    lv_obj_t* hr_key2 = lv_label_create(s_headroom_cont);
    lv_label_set_text(hr_key2, "Compression");
    lv_obj_set_style_text_font(hr_key2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hr_key2, lv_color_hex(0xaaaacc), 0);
    lv_obj_align(hr_key2, LV_ALIGN_TOP_LEFT, 15, 36);
    s_lbl_compression = lv_label_create(s_headroom_cont);
    lv_label_set_text(s_lbl_compression, "---");
    lv_obj_set_style_text_font(s_lbl_compression, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_compression, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_lbl_compression, LV_ALIGN_TOP_RIGHT, -15, 36);
    y += 64;

    /* Bottom bar */
    lv_obj_t* bot_bar = lv_obj_create(scr);
    lv_obj_set_size(bot_bar, LV_HOR_RES, 36);
    lv_obj_align(bot_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bot_bar, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(bot_bar, 0, 0);
    lv_obj_set_style_pad_all(bot_bar, 0, 0);
    lv_obj_set_style_radius(bot_bar, 0, 0);

    s_lbl_updated = lv_label_create(bot_bar);
    lv_label_set_text(s_lbl_updated, "Updated --:--:--");
    lv_obj_set_style_text_font(s_lbl_updated, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_updated, lv_color_hex(0x888899), 0);
    lv_obj_align(s_lbl_updated, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* refresh_btn = lv_button_create(bot_bar);
    lv_obj_set_size(refresh_btn, 36, 28);
    lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_radius(refresh_btn, 6, 0);
    lv_obj_add_event_cb(refresh_btn, refresh_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* refresh_lbl = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_lbl, LV_SYMBOL_REFRESH);
    lv_obj_center(refresh_lbl);

    /* Timers */
    s_poll_timer = lv_timer_create(poll_cb, 60000, NULL);
    s_immediate_timer = lv_timer_create(immediate_poll_cb, 100, NULL);
    lv_timer_set_repeat_count(s_immediate_timer, 1);

    /* Set title to active profile */
    update_title();

    LOG_I("claude_w", "Screen created, direct HTTPS mode, timers armed");
    return scr;
}

void delete_screen() {
    if (s_poll_timer)      { lv_timer_delete(s_poll_timer);      s_poll_timer      = nullptr; }
    if (s_immediate_timer) { lv_timer_delete(s_immediate_timer); s_immediate_timer = nullptr; }
    s_screen          = nullptr;
    s_dot_connected   = nullptr;
    s_lbl_model       = nullptr;
    s_bar_5h          = nullptr;
    s_lbl_5h_pct      = nullptr;
    s_lbl_5h_reset    = nullptr;
    s_bar_7d          = nullptr;
    s_lbl_7d_pct      = nullptr;
    s_lbl_7d_reset    = nullptr;
    s_lbl_cost        = nullptr;
    s_lbl_context     = nullptr;
    s_lbl_cache_ttl   = nullptr;
    s_headroom_cont   = nullptr;
    s_lbl_tokens_saved= nullptr;
    s_lbl_compression = nullptr;
    s_lbl_updated     = nullptr;
    s_badge_stale     = nullptr;
    s_badge_expired   = nullptr;
    s_lbl_status      = nullptr;
    s_lbl_title       = nullptr;
    LOG_I("claude_w", "Screen deleted");
}

} // namespace claude_widget

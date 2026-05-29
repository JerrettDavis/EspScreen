/**
 * claude_widget.cpp — Live Claude usage dashboard for EspScreen.
 *
 * Polls GET <endpoint>/status.json every 60 seconds.
 * Parses the JSON response and updates LVGL bars + labels.
 * WiFi and endpoint URL are NVS-backed (provisioned via serial).
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
#include "../../os/wifi_mgr.h"
#include "../../ui/widgets.h"
#include <lvgl.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <time.h>

namespace claude_widget {

/* ── NVS keys ────────────────────────────────────────────────────── */
static const char* NVS_NS       = "claude";
static const char* KEY_ENDPOINT = "endpoint";

/* ── Timer / screen state ────────────────────────────────────────── */
static lv_timer_t* s_poll_timer      = nullptr;
static lv_timer_t* s_immediate_timer = nullptr;
static lv_obj_t*   s_screen          = nullptr;

/* ── UI element handles ──────────────────────────────────────────── */
static lv_obj_t* s_dot_connected   = nullptr;  // small circle top-right of model row
static lv_obj_t* s_lbl_model       = nullptr;
static lv_obj_t* s_bar_5h          = nullptr;
static lv_obj_t* s_lbl_5h_pct      = nullptr;
static lv_obj_t* s_lbl_5h_reset    = nullptr;
static lv_obj_t* s_bar_7d          = nullptr;
static lv_obj_t* s_lbl_7d_pct      = nullptr;
static lv_obj_t* s_lbl_7d_reset    = nullptr;
static lv_obj_t* s_lbl_cost        = nullptr;
static lv_obj_t* s_lbl_context     = nullptr;
static lv_obj_t* s_lbl_cache_ttl   = nullptr;
static lv_obj_t* s_headroom_cont   = nullptr;  // container — hidden when disabled
static lv_obj_t* s_lbl_tokens_saved= nullptr;
static lv_obj_t* s_lbl_compression = nullptr;
static lv_obj_t* s_lbl_updated     = nullptr;
static lv_obj_t* s_badge_stale     = nullptr;  // hidden unless stale==true
static lv_obj_t* s_lbl_status      = nullptr;  // status/error message line

/* ── Helpers ─────────────────────────────────────────────────────── */

/* util is 0-100 integer percentage */
static lv_color_t bar_color(int util) {
    if (util >= 85) return lv_color_hex(0xEF4444);  // red
    if (util >= 60) return lv_color_hex(0xFBBF24);  // amber
    return lv_color_hex(0x4ADE80);                   // green
}

/* Format seconds into human-readable "Xd Yh" or "Xm" string */
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
    if (stale) {
        lv_obj_remove_flag(s_badge_stale, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_badge_stale, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_connected(bool ok) {
    if (!s_dot_connected) return;
    lv_obj_set_style_bg_color(s_dot_connected,
        ok ? lv_color_hex(0x4ADE80) : lv_color_hex(0xEF4444), 0);
}

static void set_status(const char* msg) {
    if (s_lbl_status) lv_label_set_text(s_lbl_status, msg);
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

/* ── JSON parsing + UI update ────────────────────────────────────── */

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

    /* stale badge */
    bool stale = doc["stale"] | false;
    show_stale(stale);

    /* rates — utilization is already an integer 0-100 (e.g. 65 means 65%) */
    JsonObject rates = doc["rates"];
    if (!rates.isNull()) {
        int pct5h   = rates["five_hour"]["utilization"]  | 0;
        int32_t reset5h = rates["five_hour"]["resets_in_sec"]  | 0;
        int pct7d   = rates["seven_day"]["utilization"]  | 0;
        int32_t reset7d = rates["seven_day"]["resets_in_sec"]  | 0;

        set_bar_value(s_bar_5h, pct5h);
        set_bar_value(s_bar_7d, pct7d);

        char pct_buf[16], dur_buf[24];
        snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct5h);
        lv_label_set_text(s_lbl_5h_pct, pct_buf);

        fmt_duration(dur_buf, sizeof(dur_buf), reset5h);
        char reset_buf[40];
        snprintf(reset_buf, sizeof(reset_buf), "Resets in %s", dur_buf);
        lv_label_set_text(s_lbl_5h_reset, reset_buf);

        snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct7d);
        lv_label_set_text(s_lbl_7d_pct, pct_buf);

        fmt_duration(dur_buf, sizeof(dur_buf), reset7d);
        snprintf(reset_buf, sizeof(reset_buf), "Resets in %s", dur_buf);
        lv_label_set_text(s_lbl_7d_reset, reset_buf);
    }

    /* session — may be null when no session active */
    JsonObject session = doc["session"];
    if (!session.isNull() && !session["model"].isNull()) {
        const char* model = session["model"] | "---";
        lv_label_set_text(s_lbl_model, model);

        float cost = session["cost_usd"] | 0.0f;
        char cost_buf[20];
        snprintf(cost_buf, sizeof(cost_buf), "$%.3f", cost);
        lv_label_set_text(s_lbl_cost, cost_buf);

        int ctx_pct = session["context_used_pct"] | 0;
        char ctx_buf[16];
        snprintf(ctx_buf, sizeof(ctx_buf), "%d%%", ctx_pct);
        lv_label_set_text(s_lbl_context, ctx_buf);

        int32_t ttl = session["cache_ttl_sec"] | 0;
        char ttl_buf[24];
        fmt_duration(ttl_buf, sizeof(ttl_buf), ttl);
        lv_label_set_text(s_lbl_cache_ttl, ttl_buf);
    } else {
        lv_label_set_text(s_lbl_model, "No session");
        lv_label_set_text(s_lbl_cost, "---");
        lv_label_set_text(s_lbl_context, "---");
        lv_label_set_text(s_lbl_cache_ttl, "---");
    }

    /* headroom */
    JsonObject headroom = doc["headroom"];
    bool hr_enabled = !headroom.isNull() && (headroom["enabled"] | false);
    if (s_headroom_cont) {
        if (hr_enabled) {
            lv_obj_remove_flag(s_headroom_cont, LV_OBJ_FLAG_HIDDEN);
            int32_t tokens_saved = headroom["tokens_saved"] | 0;
            int comp_pct = headroom["compression_pct"] | 0;

            char tok_buf[24], comp_buf[16];
            /* Format with thousands separator */
            if (tokens_saved >= 1000) {
                snprintf(tok_buf, sizeof(tok_buf), "%d,%03d",
                         (int)(tokens_saved / 1000), (int)(tokens_saved % 1000));
            } else {
                snprintf(tok_buf, sizeof(tok_buf), "%d", (int)tokens_saved);
            }
            snprintf(comp_buf, sizeof(comp_buf), "%d%%", comp_pct);

            lv_label_set_text(s_lbl_tokens_saved, tok_buf);
            lv_label_set_text(s_lbl_compression, comp_buf);
        } else {
            lv_obj_add_flag(s_headroom_cont, LV_OBJ_FLAG_HIDDEN);
        }
    }

    stamp_update_time();
}

/* ── HTTP poll ───────────────────────────────────────────────────── */

void poll_now() {
    /* Guard: WiFi must be up */
    if (!wifi_mgr::is_connected()) {
        set_connected(false);
        set_status("WiFi not connected");
        LOG_W("claude_w", "poll_now: WiFi not connected");
        return;
    }

    /* Guard: endpoint must be configured */
    String url = nvs_store::get_str(NVS_NS, KEY_ENDPOINT, "");
    if (url.isEmpty()) {
        set_connected(false);
        set_status("No endpoint configured");
        LOG_W("claude_w", "poll_now: no endpoint in NVS");
        return;
    }

    LOG_I("claude_w", "Polling %s", url.c_str());

    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000);

    int code = http.GET();
    if (code == 200) {
        String body = http.getString();
        http.end();
        LOG_I("claude_w", "Poll OK (%d bytes)", body.length());
        apply_json(body.c_str());
    } else {
        http.end();
        char err_buf[48];
        snprintf(err_buf, sizeof(err_buf), "HTTP error: %d", code);
        LOG_W("claude_w", "%s", err_buf);
        set_status(err_buf);
        set_connected(false);
    }
}

/* ── Timer callbacks ─────────────────────────────────────────────── */

static void poll_cb(lv_timer_t* /*t*/) {
    /* Only poll if this widget screen is currently active */
    if (s_screen && lv_scr_act() == s_screen) {
        poll_now();
    }
}

static void immediate_poll_cb(lv_timer_t* t) {
    lv_timer_delete(t);
    s_immediate_timer = nullptr;
    if (s_screen && lv_scr_act() == s_screen) {
        poll_now();
    }
}

static void back_cb(lv_event_t* /*e*/) {
    screen_router::pop();
}

static void refresh_btn_cb(lv_event_t* /*e*/) {
    poll_now();
}

/* ── Layout helpers ──────────────────────────────────────────────── */

/* Add a divider line */
static lv_obj_t* add_divider(lv_obj_t* parent, lv_coord_t y) {
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_set_size(line, 290, 1);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(line, lv_color_hex(0x444466), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    return line;
}

/* Add a key/value row: left-aligned label + right-aligned value */
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

/* Add a section header label */
static void add_section_label(lv_obj_t* parent, const char* text, lv_coord_t y) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x8888aa), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);
}

/* Add a progress bar row with a label above (key on left, pct on right) */
static lv_obj_t* add_bar_row(lv_obj_t* parent, const char* key_text, lv_coord_t y,
                               lv_obj_t** pct_label_out, lv_obj_t** reset_label_out) {
    /* Key label */
    lv_obj_t* key = lv_label_create(parent);
    lv_label_set_text(key, key_text);
    lv_obj_set_style_text_font(key, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(key, lv_color_hex(0xaaaacc), 0);
    lv_obj_align(key, LV_ALIGN_TOP_LEFT, 15, y);

    /* Percentage label (right-aligned on same row) */
    lv_obj_t* pct = lv_label_create(parent);
    lv_label_set_text(pct, "0%");
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pct, lv_color_hex(0xffffff), 0);
    lv_obj_align(pct, LV_ALIGN_TOP_RIGHT, -15, y);
    *pct_label_out = pct;

    /* Bar */
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 290, 14);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, y + 18);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x333355), 0);  // track
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x4ADE80), LV_PART_INDICATOR);  // fill default green

    /* Reset label */
    lv_obj_t* reset = lv_label_create(parent);
    lv_label_set_text(reset, "Resets in --");
    lv_obj_set_style_text_font(reset, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(reset, lv_color_hex(0x888899), 0);
    lv_obj_align(reset, LV_ALIGN_TOP_LEFT, 15, y + 36);
    *reset_label_out = reset;

    return bar;
}

/* ── Screen builder ──────────────────────────────────────────────── */

lv_obj_t* create_screen() {
    /* Safety: clean up if somehow called twice */
    if (s_poll_timer) { lv_timer_delete(s_poll_timer); s_poll_timer = nullptr; }
    if (s_immediate_timer) { lv_timer_delete(s_immediate_timer); s_immediate_timer = nullptr; }
    s_screen = nullptr;

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d0d1a), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    s_screen = scr;

    /* ── Top bar ──────────────────────────────────────────────────── */
    lv_obj_t* topbar = lv_obj_create(scr);
    lv_obj_set_size(topbar, LV_HOR_RES, 36);
    lv_obj_align(topbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_pad_all(topbar, 0, 0);
    lv_obj_set_style_radius(topbar, 0, 0);

    /* Back button — 40x30 top-left */
    lv_obj_t* back_btn = lv_button_create(topbar);
    lv_obj_set_size(back_btn, 48, 28);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_radius(back_btn, 6, 0);
    lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    /* Title */
    lv_obj_t* title = lv_label_create(topbar);
    lv_label_set_text(title, "Claude Usage");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* Stale badge — hidden by default */
    s_badge_stale = lv_label_create(topbar);
    lv_label_set_text(s_badge_stale, "stale!");
    lv_obj_set_style_text_color(s_badge_stale, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_text_font(s_badge_stale, &lv_font_montserrat_14, 0);
    lv_obj_align(s_badge_stale, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_flag(s_badge_stale, LV_OBJ_FLAG_HIDDEN);

    /* ── Content area (scrollable) ────────────────────────────────── */
    /* We use absolute y positions on the screen so everything is visible
     * in a 480px height. Topbar=36px + content fits in 444px remaining. */

    /* ── Model row ────────────────────────────────────────────────── */
    lv_coord_t y = 44;

    s_lbl_model = lv_label_create(scr);
    lv_label_set_text(s_lbl_model, "---");
    lv_obj_set_style_text_font(s_lbl_model, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_model, lv_color_hex(0xddddff), 0);
    lv_obj_align(s_lbl_model, LV_ALIGN_TOP_LEFT, 15, y);

    /* Connection dot */
    s_dot_connected = lv_obj_create(scr);
    lv_obj_set_size(s_dot_connected, 12, 12);
    lv_obj_align(s_dot_connected, LV_ALIGN_TOP_RIGHT, -15, y + 2);
    lv_obj_set_style_radius(s_dot_connected, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot_connected, lv_color_hex(0xEF4444), 0);  // starts red
    lv_obj_set_style_border_width(s_dot_connected, 0, 0);
    lv_obj_set_style_pad_all(s_dot_connected, 0, 0);

    y += 20;
    add_divider(scr, y);

    /* ── Status/error line ────────────────────────────────────────── */
    y += 6;
    s_lbl_status = lv_label_create(scr);
    lv_label_set_text(s_lbl_status, "");
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xEF4444), 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_TOP_MID, 0, y);

    /* ── 5-hour bar ───────────────────────────────────────────────── */
    y += 14;
    s_bar_5h = add_bar_row(scr, "5-hour limit", y, &s_lbl_5h_pct, &s_lbl_5h_reset);
    y += 56;  // key(16) + bar(18) + reset(16) + gap(6)

    /* ── 7-day bar ────────────────────────────────────────────────── */
    s_bar_7d = add_bar_row(scr, "7-day limit", y, &s_lbl_7d_pct, &s_lbl_7d_reset);
    y += 56;

    /* ── Session section ──────────────────────────────────────────── */
    add_divider(scr, y);
    y += 6;
    add_section_label(scr, "--- This session ---", y);
    y += 18;

    add_kv_row(scr, "Cost", y, &s_lbl_cost);       y += 18;
    add_kv_row(scr, "Context used", y, &s_lbl_context);   y += 18;
    add_kv_row(scr, "Cache TTL", y, &s_lbl_cache_ttl);    y += 22;

    /* ── Headroom section ─────────────────────────────────────────── */
    add_divider(scr, y);
    y += 6;

    /* Container for headroom section so we can hide it as a unit */
    s_headroom_cont = lv_obj_create(scr);
    lv_obj_set_size(s_headroom_cont, LV_HOR_RES, 58);
    lv_obj_align(s_headroom_cont, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_opa(s_headroom_cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_headroom_cont, 0, 0);
    lv_obj_set_style_pad_all(s_headroom_cont, 0, 0);
    lv_obj_add_flag(s_headroom_cont, LV_OBJ_FLAG_HIDDEN);  // default hidden

    /* Children of headroom container — positioned relative to it */
    lv_obj_t* hr_title = lv_label_create(s_headroom_cont);
    lv_label_set_text(hr_title, "--- Headroom ---");
    lv_obj_set_style_text_font(hr_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hr_title, lv_color_hex(0x8888aa), 0);
    lv_obj_align(hr_title, LV_ALIGN_TOP_MID, 0, 0);

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

    y += 64;  // headroom container height + gap

    /* ── Bottom bar: timestamp + refresh button ───────────────────── */
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

    /* Refresh button */
    lv_obj_t* refresh_btn = lv_button_create(bot_bar);
    lv_obj_set_size(refresh_btn, 36, 28);
    lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_radius(refresh_btn, 6, 0);
    lv_obj_add_event_cb(refresh_btn, refresh_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* refresh_lbl = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_lbl, LV_SYMBOL_REFRESH);
    lv_obj_center(refresh_lbl);

    /* ── Timers ────────────────────────────────────────────────────── */
    /* Recurring 60s poll */
    s_poll_timer = lv_timer_create(poll_cb, 60000, NULL);

    /* Immediate first poll — fires after 100ms so screen finishes loading */
    s_immediate_timer = lv_timer_create(immediate_poll_cb, 100, NULL);
    lv_timer_set_repeat_count(s_immediate_timer, 1);

    LOG_I("claude_w", "Screen created, timers armed");
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
    s_lbl_status      = nullptr;
    LOG_I("claude_w", "Screen deleted, timers stopped");
}

} // namespace claude_widget

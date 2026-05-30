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
 *
 * Threading model (v2 — non-blocking):
 *   Network runs in a FreeRTOS task on core 0 (poll_task).
 *   Results are delivered via a single-slot queue (s_result_q) to the LVGL thread.
 *   s_result_q is created once (lazy init) and NEVER deleted during the app's
 *   lifetime — this avoids a use-after-free race between a still-running poll
 *   task and screen_delete_cb destroying the queue. The queue simply persists
 *   until the next open, which drains any stale result harmlessly.
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
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_heap_caps.h>

namespace claude_widget {

/* ── Direct API endpoint (no longer NVS-backed) ──────────────────────── */
static const char* ANTHROPIC_USAGE_URL = "https://api.anthropic.com/api/oauth/usage";
static const char* ANTHROPIC_BETA_HDR  = "oauth-2025-04-20";

/* ── Poll result struct — filled by poll_task, read by apply_cb ───────── */
struct PollResult {
    bool  ok;           // true = HTTP 200 + valid JSON
    int   http_code;    // raw HTTP status (or -1 for network error)
    int   pct5h;
    int   pct7d;
    char  reset5h[24];  // formatted duration string, e.g. "2h 15m"
    char  reset7d[24];
    char  err[64];      // error message when ok==false
    bool  token_expired;
};

/* ── Background task / queue state ───────────────────────────────────── */
/* s_result_q: single-slot queue, persistent across screen open/close.
 * Created lazily on first trigger_poll() and never deleted.
 * Avoids use-after-free if poll_task is still writing when screen closes. */
static QueueHandle_t s_result_q    = nullptr;
static volatile bool s_poll_inflight = false;  // true while poll_task is running
static TaskHandle_t  s_poll_task   = nullptr;

/* ── Timer / screen state ─────────────────────────────────────────────── */
static lv_timer_t* s_poll_timer      = nullptr;
static lv_timer_t* s_immediate_timer = nullptr;
static lv_timer_t* s_apply_timer     = nullptr;  // 250ms LVGL-thread drain
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

/* ── ISO-8601 timestamp → seconds-until helper (no LVGL, safe for task) ─ */
static int32_t parse_reset_secs(const char* ts) {
    if (!ts) return 0;
    struct tm t = {};
    char* p = strptime(ts, "%Y-%m-%dT%H:%M:%S", &t);
    if (!p) return 0;
    /* Skip optional fractional seconds */
    if (*p == '.') { ++p; while (*p >= '0' && *p <= '9') ++p; }
    int tz_offset_secs = 0;
    if (*p == 'Z' || *p == 'z') {
        tz_offset_secs = 0;
    } else if (*p == '+' || *p == '-') {
        int sign = (*p == '+') ? 1 : -1;
        ++p;
        int hh = 0, mm = 0;
        sscanf(p, "%2d:%2d", &hh, &mm);
        tz_offset_secs = sign * (hh * 3600 + mm * 60);
    }
    t.tm_isdst = 0;
    time_t local_epoch = mktime(&t);
    time_t target = local_epoch - tz_offset_secs;
    int32_t secs = (int32_t)(target - time(nullptr));
    return secs < 0 ? 0 : secs;
}

/* ── Apply PollResult to LVGL widgets — LVGL thread only ─────────────── */
static void apply_result(const PollResult& res) {
    /* Guard: screen may have been destroyed while task was running */
    if (!s_screen) return;

    if (!res.ok) {
        set_connected(false);
        set_status(res.err);
        show_expired(res.token_expired);
        return;
    }

    set_connected(true);
    set_status("");
    show_stale(false);
    show_expired(false);

    set_bar_value(s_bar_5h, res.pct5h);
    char pct_buf[16];
    snprintf(pct_buf, sizeof(pct_buf), "%d%%", res.pct5h);
    if (s_lbl_5h_pct)   lv_label_set_text(s_lbl_5h_pct, pct_buf);
    char reset_buf[40];
    snprintf(reset_buf, sizeof(reset_buf), "Resets in %s", res.reset5h);
    if (s_lbl_5h_reset) lv_label_set_text(s_lbl_5h_reset, reset_buf);

    set_bar_value(s_bar_7d, res.pct7d);
    snprintf(pct_buf, sizeof(pct_buf), "%d%%", res.pct7d);
    if (s_lbl_7d_pct)   lv_label_set_text(s_lbl_7d_pct, pct_buf);
    snprintf(reset_buf, sizeof(reset_buf), "Resets in %s", res.reset7d);
    if (s_lbl_7d_reset) lv_label_set_text(s_lbl_7d_reset, reset_buf);

    /* Session fields — not present in the usage API, show placeholders */
    if (s_lbl_model)     lv_label_set_text(s_lbl_model, "Claude");
    if (s_lbl_cost)      lv_label_set_text(s_lbl_cost, "---");
    if (s_lbl_context)   lv_label_set_text(s_lbl_context, "---");
    if (s_lbl_cache_ttl) lv_label_set_text(s_lbl_cache_ttl, "---");

    /* Headroom — not available in direct-API mode */
    if (s_headroom_cont) lv_obj_add_flag(s_headroom_cont, LV_OBJ_FLAG_HIDDEN);

    update_title();
    stamp_update_time();
}

/* ── NETWORK PART — runs inside poll_task, NO LVGL calls allowed ──────── */
/*
 * Performs the complete HTTP round-trip (including token refresh if needed)
 * and fills out a PollResult. Does NOT touch s_screen or any lv_* API.
 * All String temporaries are destroyed before xQueueOverwrite, keeping
 * peak heap low during the handoff.
 */
static void do_network_poll(PollResult& result) {
    memset(&result, 0, sizeof(result));
    result.ok = false;

    if (!wifi_profiles::is_connected()) {
        snprintf(result.err, sizeof(result.err), "WiFi not connected");
        LOG_W("claude_w", "poll_task: WiFi not connected");
        return;
    }

    bool is_expired = false;
    String token = claude_auth::get_active_access_token(&is_expired);

    if (token.isEmpty()) {
        if (claude_auth::profile_count() == 0) {
            snprintf(result.err, sizeof(result.err), "No Claude profile configured");
        } else {
            snprintf(result.err, sizeof(result.err), "No access token configured");
            result.token_expired = true;
        }
        LOG_W("claude_w", "poll_task: %s", result.err);
        return;
    }

    /* Proactive refresh if token is expired */
    if (is_expired) {
        LOG_I("claude_w", "poll_task: token expired — attempting refresh");
        if (claude_auth::refresh_active()) {
            LOG_I("claude_w", "poll_task: token refreshed successfully");
            token = claude_auth::get_active_access_token(&is_expired);
        } else {
            LOG_W("claude_w", "poll_task: refresh failed — attempting poll with stale token");
            result.token_expired = true;
        }
    }

    LOG_I("claude_w", "poll_task pre-HTTPS heap: %lu", (unsigned long)esp_get_free_heap_size());

    /* ── First attempt ─────────────────────────────────────────────── */
    WiFiClientSecure client;
    client.setInsecure();  // TODO: pin cert for production

    HTTPClient http;
    http.begin(client, ANTHROPIC_USAGE_URL);
    http.setTimeout(10000);
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("anthropic-beta", ANTHROPIC_BETA_HDR);
    http.addHeader("Content-Type", "application/json");

    int code = http.GET();
    result.http_code = code;

    LOG_I("claude_w", "poll_task post-HTTPS heap: %lu  HTTP=%d",
          (unsigned long)esp_get_free_heap_size(), code);

    if (code == 401) {
        http.end();
        client.stop();
        /* 401 — attempt one refresh-and-retry cycle */
        LOG_W("claude_w", "poll_task: 401 — attempting token refresh");
        if (claude_auth::refresh_active()) {
            token = claude_auth::get_active_access_token(nullptr);
            WiFiClientSecure client2;
            client2.setInsecure();
            HTTPClient http2;
            http2.begin(client2, ANTHROPIC_USAGE_URL);
            http2.setTimeout(10000);
            http2.addHeader("Authorization", "Bearer " + token);
            http2.addHeader("anthropic-beta", ANTHROPIC_BETA_HDR);
            http2.addHeader("Content-Type", "application/json");
            code = http2.GET();
            result.http_code = code;
            if (code == 200) {
                String body2 = http2.getString();
                http2.end();
                client2.stop();
                /* Parse JSON */
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, body2);
                if (err) {
                    snprintf(result.err, sizeof(result.err), "JSON error: %s", err.c_str());
                    LOG_W("claude_w", "poll_task: %s", result.err);
                    return;
                }
                JsonObject fh = doc["five_hour"];
                if (!fh.isNull()) {
                    result.pct5h = (int)roundf(fh["utilization"] | 0.0f);
                    int32_t secs5h = parse_reset_secs(fh["resets_at"] | (const char*)nullptr);
                    fmt_duration(result.reset5h, sizeof(result.reset5h), secs5h);
                }
                JsonObject sd = doc["seven_day"];
                if (!sd.isNull()) {
                    result.pct7d = (int)roundf(sd["utilization"] | 0.0f);
                    int32_t secs7d = parse_reset_secs(sd["resets_at"] | (const char*)nullptr);
                    fmt_duration(result.reset7d, sizeof(result.reset7d), secs7d);
                }
                result.ok = true;
                LOG_I("claude_w", "poll_task: retry after refresh succeeded");
            } else {
                http2.end();
                client2.stop();
                result.token_expired = true;
                snprintf(result.err, sizeof(result.err), "Auth failed — reprovision tokens");
                LOG_W("claude_w", "poll_task: retry after refresh failed: %d", code);
            }
        } else {
            result.token_expired = true;
            snprintf(result.err, sizeof(result.err), "Token expired — use 'claude token set'");
            LOG_W("claude_w", "poll_task: refresh failed");
        }
        return;
    }

    if (code == 200) {
        String body = http.getString();
        http.end();
        client.stop();
        /* Parse JSON */
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) {
            snprintf(result.err, sizeof(result.err), "JSON error: %s", err.c_str());
            LOG_W("claude_w", "poll_task: %s", result.err);
            return;
        }
        LOG_I("claude_w", "poll_task: Poll OK (%d bytes)", body.length());
        JsonObject fh = doc["five_hour"];
        if (!fh.isNull()) {
            result.pct5h = (int)roundf(fh["utilization"] | 0.0f);
            int32_t secs5h = parse_reset_secs(fh["resets_at"] | (const char*)nullptr);
            fmt_duration(result.reset5h, sizeof(result.reset5h), secs5h);
        }
        JsonObject sd = doc["seven_day"];
        if (!sd.isNull()) {
            result.pct7d = (int)roundf(sd["utilization"] | 0.0f);
            int32_t secs7d = parse_reset_secs(sd["resets_at"] | (const char*)nullptr);
            fmt_duration(result.reset7d, sizeof(result.reset7d), secs7d);
        }
        result.ok = true;
    } else {
        String body = http.getString();
        http.end();
        client.stop();
        snprintf(result.err, sizeof(result.err), "HTTP error: %d", code);
        LOG_W("claude_w", "poll_task: %s", result.err);
    }
}

/* ── FreeRTOS poll task — core 0, 20KB stack ─────────────────────────── */
/*
 * Stack is 20480 bytes (bumped from 16384) to ensure TLS handshake fits
 * comfortably (TLS requires ~14-16KB stack headroom on ESP32).
 *
 * RULES:
 *   - NEVER call any lv_* function from this task.
 *   - NEVER dereference s_screen from this task.
 *   - Only write to s_result_q and clear s_poll_inflight.
 */
static void poll_task(void* /*arg*/) {
    PollResult result;
    do_network_poll(result);

    /* Deliver to LVGL thread — xQueueOverwrite never blocks */
    if (s_result_q) {
        xQueueOverwrite(s_result_q, &result);
    }

    s_poll_inflight = false;
    s_poll_task = nullptr;
    LOG_I("claude_w", "poll_task: done, post-task heap=%lu",
          (unsigned long)esp_get_free_heap_size());
    vTaskDelete(NULL);
}

/* ── trigger_poll — non-blocking, safe to call from LVGL thread ──────── */
static void trigger_poll() {
    if (s_poll_inflight) {
        LOG_I("claude_w", "trigger_poll: already in flight, skipping");
        return;
    }

    /* Lazy-init the persistent queue (created once, never deleted) */
    if (!s_result_q) {
        s_result_q = xQueueCreate(1, sizeof(PollResult));
        if (!s_result_q) {
            LOG_W("claude_w", "trigger_poll: failed to create result queue");
            return;
        }
    }

    s_poll_inflight = true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        poll_task,      /* task function */
        "clpoll",       /* name */
        20480,          /* stack bytes — 20KB for TLS headroom */
        nullptr,        /* arg */
        1,              /* priority */
        &s_poll_task,   /* handle */
        0               /* core 0 — leaves core 1 free for LVGL/touch */
    );
    if (ok != pdPASS) {
        s_poll_inflight = false;
        LOG_W("claude_w", "trigger_poll: xTaskCreatePinnedToCore failed");
        return;
    }
    LOG_I("claude_w", "trigger_poll: task spawned, free heap=%lu",
          (unsigned long)esp_get_free_heap_size());
}

/* ── apply_cb — 250ms LVGL-thread timer, drains the result queue ──────── */
/*
 * Runs on the LVGL thread every 250ms.
 * xQueueReceive(..., 0) is non-blocking — returns immediately if empty.
 * Only applies the result if the screen is still the active screen.
 */
static void apply_cb(lv_timer_t* /*t*/) {
    if (!s_result_q) return;
    PollResult res;
    if (xQueueReceive(s_result_q, &res, 0) == pdTRUE) {
        if (s_screen && lv_scr_act() == s_screen) {
            apply_result(res);
        }
    }
}

/* ── Screen delete event — null all widget statics and timers ──────────── */
/*
 * Design choice: s_result_q is NOT deleted here.
 * Reason: poll_task may still be mid-flight writing to the queue when the
 * screen closes. Deleting the queue would cause a use-after-free in the task.
 * Instead: null s_screen so apply_cb and apply_result become no-ops, and let
 * the task finish and self-delete. The queue is reused on next screen open.
 * A stale result sitting in the queue when the screen reopens is harmlessly
 * consumed (or overwritten) by the next trigger_poll.
 */
static void screen_delete_cb(lv_event_t* /*e*/) {
    /* Stop timers so no further poll/apply callbacks fire after screen is freed */
    if (s_poll_timer)      { lv_timer_delete(s_poll_timer);      s_poll_timer      = nullptr; }
    if (s_immediate_timer) { lv_timer_delete(s_immediate_timer); s_immediate_timer = nullptr; }
    if (s_apply_timer)     { lv_timer_delete(s_apply_timer);     s_apply_timer     = nullptr; }
    /* Null s_screen FIRST — poll_task checks this indirectly via apply_result */
    s_screen          = nullptr;
    /* Null all widget pointers — apply_result will no-op on every widget call */
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
    /* Note: s_poll_inflight and s_poll_task are left intact — the task
     * will clear them when it finishes. Do not cancel/delete the task here
     * as it may be mid-TLS and cancellation would leak the SSL context. */
    LOG_I("claude_w", "screen_delete_cb: timers stopped, widget statics nulled");
}

/* ── Timer callbacks ──────────────────────────────────────────────────── */

static void poll_cb(lv_timer_t* /*t*/) {
    if (s_screen && lv_scr_act() == s_screen) trigger_poll();
}

static void immediate_poll_cb(lv_timer_t* t) {
    lv_timer_delete(t);
    s_immediate_timer = nullptr;
    if (s_screen && lv_scr_act() == s_screen) trigger_poll();
}

static void back_cb(lv_event_t* /*e*/) { screen_router::pop(); }
static void refresh_btn_cb(lv_event_t* /*e*/) { trigger_poll(); }

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
    if (s_poll_timer)      { lv_timer_delete(s_poll_timer);      s_poll_timer      = nullptr; }
    if (s_immediate_timer) { lv_timer_delete(s_immediate_timer); s_immediate_timer = nullptr; }
    if (s_apply_timer)     { lv_timer_delete(s_apply_timer);     s_apply_timer     = nullptr; }
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

    /* Register delete callback so timers and widget statics are nulled if
     * LVGL frees this screen (e.g. lv_obj_del called from outside this widget). */
    lv_obj_add_event_cb(scr, screen_delete_cb, LV_EVENT_DELETE, nullptr);

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

    /* Timers:
     *   s_poll_timer      — 60s periodic, triggers background poll
     *   s_immediate_timer — one-shot 100ms, kicks off the first poll after load
     *   s_apply_timer     — 250ms periodic, drains s_result_q on LVGL thread
     */
    s_poll_timer      = lv_timer_create(poll_cb, 60000, NULL);
    s_immediate_timer = lv_timer_create(immediate_poll_cb, 100, NULL);
    lv_timer_set_repeat_count(s_immediate_timer, 1);
    s_apply_timer     = lv_timer_create(apply_cb, 250, NULL);

    /* Set title to active profile */
    update_title();

    LOG_I("claude_w", "Screen created, direct HTTPS mode (async), timers armed");
    return scr;
}

/* ── poll_now — public API, called by serial command ─────────────────── */
/*
 * Triggers a background poll. Non-blocking: returns immediately.
 * Callers that used to rely on synchronous completion should now rely on
 * the 250ms apply_cb drain to see widget updates.
 */
void poll_now() {
    if (!s_screen) {
        LOG_I("claude_w", "poll_now: no screen — skipping (open the Claude tile first)");
        return;
    }
    trigger_poll();
}

void delete_screen() {
    if (s_poll_timer)      { lv_timer_delete(s_poll_timer);      s_poll_timer      = nullptr; }
    if (s_immediate_timer) { lv_timer_delete(s_immediate_timer); s_immediate_timer = nullptr; }
    if (s_apply_timer)     { lv_timer_delete(s_apply_timer);     s_apply_timer     = nullptr; }
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

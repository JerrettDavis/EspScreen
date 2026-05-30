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
#include "../../ui/theme.h"
#include "../../ui/tokens.h"
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
    bool     ok;           // true = HTTP 200 + valid JSON
    int      http_code;    // raw HTTP status (or -1 for network error)
    int      pct5h;
    int      pct7d;
    char     reset5h[24];  // formatted duration string, e.g. "2h 15m"
    char     reset7d[24];
    char     err[64];      // error message when ok==false
    bool     token_expired;
    uint32_t session;      // screen-session id captured at trigger_poll time
};

/* ── Background task / queue state ───────────────────────────────────── */
/* s_result_q: single-slot queue, persistent across screen open/close.
 * Created lazily on first trigger_poll() and never deleted.
 * Avoids use-after-free if poll_task is still writing when screen closes. */
static QueueHandle_t s_result_q      = nullptr;
static volatile bool s_poll_inflight = false;  // true while poll_task is running
static TaskHandle_t  s_poll_task     = nullptr;

/* Monotonic screen-session id. Incremented every create_screen(). A PollResult
 * is tagged with the session that requested it; apply_cb discards results whose
 * session != the current one (belong to a prior, now-destroyed screen). */
static volatile uint32_t s_session      = 0;
static volatile uint32_t s_poll_session = 0;  // session captured at trigger_poll time

/* ── Timer / screen state ─────────────────────────────────────────────── */
static lv_timer_t* s_poll_timer      = nullptr;
static lv_timer_t* s_immediate_timer = nullptr;
static lv_timer_t* s_apply_timer     = nullptr;  // 250ms LVGL-thread drain
static lv_obj_t*   s_screen          = nullptr;

/* ── UI element handles ───────────────────────────────────────────────── */
static lv_obj_t*       s_dot_connected = nullptr;
static lv_obj_t*       s_lbl_model     = nullptr;
static widgets::BarRow s_row_5h        = {};
static widgets::BarRow s_row_7d        = {};
static lv_obj_t*       s_lbl_updated   = nullptr;
static lv_obj_t*       s_badge_stale   = nullptr;
static lv_obj_t*       s_badge_expired = nullptr;  // "Token expired" badge
static lv_obj_t*       s_lbl_status    = nullptr;
static lv_obj_t*       s_lbl_title     = nullptr;  // top-bar title (shows profile name)

/* ── Helpers ──────────────────────────────────────────────────────────── */

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
        ok ? lv_color_hex(tok::SUCCESS) : lv_color_hex(tok::ERROR_), 0);
}

static void set_status(const char* msg) {
    if (s_lbl_status) lv_label_set_text(s_lbl_status, msg);
}

static void update_title() {
    if (!s_lbl_title) return;
    String label = claude_auth::get_active_label();
    String title = "Claude";
    if (label != "(none)") {
        title = "Claude - " + label;  // ASCII hyphen (em dash 0x2014 is not in the font -> tofu)
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

    /* 5-hour bar */
    widgets::bar_set(s_row_5h.bar, s_row_5h.pct, res.pct5h);
    char reset_buf[40];
    snprintf(reset_buf, sizeof(reset_buf), "Resets in %s", res.reset5h);
    if (s_row_5h.reset) lv_label_set_text(s_row_5h.reset, reset_buf);

    /* 7-day bar */
    widgets::bar_set(s_row_7d.bar, s_row_7d.pct, res.pct7d);
    snprintf(reset_buf, sizeof(reset_buf), "Resets in %s", res.reset7d);
    if (s_row_7d.reset) lv_label_set_text(s_row_7d.reset, reset_buf);

    /* Model name */
    if (s_lbl_model) lv_label_set_text(s_lbl_model, "Claude");

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
                snprintf(result.err, sizeof(result.err),
                         "Refreshed but still 401 — reprovision tokens");
                LOG_W("claude_w", "poll_task: retry after refresh failed: %d", code);
            }
        } else {
            result.token_expired = true;
            if (!claude_auth::wall_clock_ready()) {
                snprintf(result.err, sizeof(result.err),
                         "Clock not synced — token refresh failed. Reprovision via 'claude token set'");
            } else {
                snprintf(result.err, sizeof(result.err),
                         "Token expired & refresh failed — run 'claude token set'");
            }
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

/* ── FreeRTOS poll task — core 0, 8KB stack ──────────────────────────── */
/*
 * Stack is 8192 bytes — the minimum for ESP-IDF TLS call frames on the stack.
 * mbedTLS data structures (SSL context, BIGNUM/PK buffers, cert store) are
 * heap-allocated by the library itself, so reducing task-stack size directly
 * reclaims heap available for those allocations. 24KB stack was too aggressive
 * on a no-PSRAM board at ~47KB free heap, causing BIGNUM malloc failures.
 *
 * RULES:
 *   - NEVER call any lv_* function from this task.
 *   - NEVER dereference s_screen from this task.
 *   - Only write to s_result_q and clear s_poll_inflight.
 */
static void poll_task(void* /*arg*/) {
    PollResult result;
    do_network_poll(result);
    result.session = s_poll_session;     // tag with requesting session

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

    s_poll_session = s_session;   // tag this poll with the requesting session
    s_poll_inflight = true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        poll_task,      /* task function */
        "clpoll",       /* name */
        8192,           /* stack bytes — 8KB; mbedTLS buffers live in heap, not stack */
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
        if (res.session != s_session) {
            LOG_I("claude_w", "apply_cb: dropping stale result (session %lu != %lu)",
                  (unsigned long)res.session, (unsigned long)s_session);
            return;
        }
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
    s_row_5h          = {};
    s_row_7d          = {};
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

static void screen_unloaded_cb(lv_event_t* e) {
    screen_delete_cb(e);                 // stop timers, null widget statics, null s_screen
    screen_router::delete_on_unload(e);  // async-delete after fade
}

/* ── Timer callbacks ──────────────────────────────────────────────────── */

static void poll_cb(lv_timer_t* /*t*/) {
    if (s_screen && lv_scr_act() == s_screen) trigger_poll();
}

static void immediate_poll_cb(lv_timer_t* t) {
    if (!s_screen || lv_scr_act() != s_screen) {
        lv_timer_delete(t);
        s_immediate_timer = nullptr;
        return;
    }
    if (s_poll_inflight) {
        /* A prior poll (old session) is still running. Wait & retry so the
         * re-opened screen still gets a fresh poll once it clears. */
        return;   // leave timer running; repeats until inflight clears
    }
    lv_timer_delete(t);
    s_immediate_timer = nullptr;
    trigger_poll();
}

static void back_cb(lv_event_t* /*e*/) { screen_router::pop(); }
static void refresh_btn_cb(lv_event_t* /*e*/) { trigger_poll(); }

/* ── Screen builder ───────────────────────────────────────────────────── */

lv_obj_t* create_screen() {
    s_session++;   // new screen instance — invalidates any in-flight prior poll's result

    if (s_poll_timer)      { lv_timer_delete(s_poll_timer);      s_poll_timer      = nullptr; }
    if (s_immediate_timer) { lv_timer_delete(s_immediate_timer); s_immediate_timer = nullptr; }
    if (s_apply_timer)     { lv_timer_delete(s_apply_timer);     s_apply_timer     = nullptr; }
    s_screen = nullptr;

    /* Check for legacy endpoint config and warn */
    String legacy_ep = nvs_store::get_str("claude", "endpoint", "");
    if (!legacy_ep.isEmpty()) {
        LOG_I("claude_w", "Legacy 'endpoint' config ignored — using direct OAuth path");
    }

    /* ── Screen ──────────────────────────────────────────────────────────── */
    lv_obj_t* scr = widgets::make_screen();
    s_screen = scr;

    /* Register unload callback: stops timers/nulls statics, then async-deletes
     * the screen after its fade-out animation completes (via screen_router). */
    lv_obj_add_event_cb(scr, screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, nullptr);

    /* ── Top bar ─────────────────────────────────────────────────────────── */
    lv_obj_t* topbar = widgets::make_topbar(scr, "Claude Usage", back_cb, &s_lbl_title);

    /* Connection dot — right side of topbar */
    s_dot_connected = widgets::make_status_dot(topbar, tok::ERROR_);
    lv_obj_align(s_dot_connected, LV_ALIGN_RIGHT_MID, -tok::SP_M, 0);

    /* Stale badge — near right of topbar, hidden by default */
    s_badge_stale = lv_label_create(topbar);
    lv_label_set_text(s_badge_stale, "stale!");
    lv_obj_set_style_text_color(s_badge_stale, lv_color_hex(tok::ERROR_), 0);
    lv_obj_add_style(s_badge_stale, ui_theme::style_topbar_title(), 0);
    lv_obj_align(s_badge_stale, LV_ALIGN_RIGHT_MID, -(tok::SP_M + 20), 0);
    lv_obj_add_flag(s_badge_stale, LV_OBJ_FLAG_HIDDEN);

    /* ── Content container — vertical flex between topbar and botbar ───────────
     * BUG FIX: previously each section was added directly to `scr` and the two
     * make_bar_row() containers were never positioned, so all sections collapsed
     * to (0,0) and overlapped at the top. We now use a single flex-COLUMN content
     * container; each section is a flex child and stacks vertically with a row gap.
     * make_bar_row(content, ...) creates its column INSIDE this container, so the
     * flex layout positions the whole bar-row block (no manual lv_obj_align needed). */
    lv_obj_t* content = lv_obj_create(scr);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - tok::TOPBAR_H - tok::BOTBAR_H);
    lv_obj_set_pos(content, 0, tok::TOPBAR_H);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, tok::SCREEN_PAD, 0);
    lv_obj_set_style_pad_row(content, tok::SP_M, 0);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* Token expired badge — hidden by default (flex child) */
    s_badge_expired = lv_label_create(content);
    lv_label_set_text(s_badge_expired, "Token expired — use 'claude token set'");
    lv_obj_set_style_text_font(s_badge_expired, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_badge_expired, lv_color_hex(tok::ERROR_), 0);
    lv_obj_set_width(s_badge_expired, LV_PCT(100));
    lv_label_set_long_mode(s_badge_expired, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_badge_expired, LV_OBJ_FLAG_HIDDEN);

    /* Status/error line (flex child) */
    s_lbl_status = lv_label_create(content);
    lv_label_set_text(s_lbl_status, "");
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(tok::ERROR_), 0);
    lv_obj_set_width(s_lbl_status, LV_PCT(100));
    lv_label_set_long_mode(s_lbl_status, LV_LABEL_LONG_WRAP);

    /* ── HERO: 5-hour bar row (flex child block) ──────────────────────────── */
    s_row_5h = widgets::make_bar_row(content, "5-hour limit");

    /* ── HERO: 7-day bar row (flex child block) ───────────────────────────── */
    s_row_7d = widgets::make_bar_row(content, "7-day limit");

    /* ── Divider + Model name kv row (flex children) ──────────────────────── */
    widgets::make_divider(content);
    s_lbl_model = widgets::make_kv_row(content, "Model");

    /* ── Bottom bar ──────────────────────────────────────────────────────── */
    lv_obj_t* botbar = widgets::make_botbar(scr);

    /* Timestamp label — left side */
    s_lbl_updated = lv_label_create(botbar);
    lv_label_set_text(s_lbl_updated, "Updated --:--:--");
    lv_obj_add_style(s_lbl_updated, ui_theme::style_text_muted(), 0);
    lv_obj_align(s_lbl_updated, LV_ALIGN_LEFT_MID, tok::SP_S, 0);

    /* Refresh button — right side (ghost style; widened for ASCII "Refresh" text) */
    lv_obj_t* refresh_btn = lv_button_create(botbar);
    lv_obj_set_size(refresh_btn, 88, 32);
    lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, -tok::SP_S, 0);
    lv_obj_add_style(refresh_btn, ui_theme::style_btn_ghost(), 0);
    lv_obj_add_event_cb(refresh_btn, refresh_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* refresh_lbl = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_lbl, "Refresh");   // ASCII (glyph-safe; FA glyphs tofu on this panel)
    lv_obj_center(refresh_lbl);

    /* Timers:
     *   s_poll_timer      — 60s periodic, triggers background poll
     *   s_immediate_timer — one-shot 100ms, kicks off the first poll after load
     *   s_apply_timer     — 250ms periodic, drains s_result_q on LVGL thread
     */
    s_poll_timer      = lv_timer_create(poll_cb, 60000, NULL);
    s_immediate_timer = lv_timer_create(immediate_poll_cb, 100, NULL);
    /* s_immediate_timer is repeating — immediate_poll_cb self-deletes once it
     * successfully fires a poll (or when the screen is no longer active). */
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
    s_row_5h          = {};
    s_row_7d          = {};
    s_lbl_updated     = nullptr;
    s_badge_stale     = nullptr;
    s_badge_expired   = nullptr;
    s_lbl_status      = nullptr;
    s_lbl_title       = nullptr;
    LOG_I("claude_w", "Screen deleted");
}

} // namespace claude_widget

/**
 * wifi_setup.cpp — On-screen WiFi scan + credential entry + connect flow.
 *
 * Three logical steps share ONE pushed screen; content is swapped:
 *   Step 1  Scan list    — scrollable flex-column of lv_button rows
 *   Step 2  Password     — lv_textarea (password mode) + lv_keyboard
 *   Step 3  Connecting   — status label; pops on success after 1.5 s
 *
 * Memory strategy (48 KB LVGL heap, no PSRAM):
 *   Scan container is deleted before the keyboard is created so the two
 *   heavyweight allocations never coexist.  Heap is logged right after
 *   keyboard creation.
 *
 * Lifecycle mirrors claude_widget.cpp:
 *   LV_EVENT_DELETE on the screen nulls all static widget pointers and
 *   deletes any pending one-shot timer.
 */

#include "wifi_setup.h"
#include "../../os/wifi_profiles.h"
#include "../../os/screen_router.h"
#include "../../os/logger.h"
#include <lvgl.h>
#include <Arduino.h>   // esp_get_free_heap_size, millis

namespace wifi_setup {

/* ── Constraints ─────────────────────────────────────────────────────── */

static constexpr uint8_t  MAX_SCAN_RESULTS = 12;
static constexpr uint32_t CONNECT_TIMEOUT_MS = 12000;
static constexpr uint32_t SUCCESS_POP_DELAY_MS = 1500;

/* ── File-static widget/data state ──────────────────────────────────── */

/* The one screen object — lives until screen_router::pop() destroys it */
static lv_obj_t* s_screen       = nullptr;

/* Step 1 — scan list container (deleted before Step 2 to free heap) */
static lv_obj_t* s_scan_cont    = nullptr;

/* Step 2 — keyboard and textarea */
static lv_obj_t* s_keyboard     = nullptr;
static lv_obj_t* s_textarea     = nullptr;

/* Step 3 — status label and hint label (both tracked to prevent leaks on re-entry) */
static lv_obj_t* s_status_lbl   = nullptr;
static lv_obj_t* s_hint_lbl     = nullptr;

/* One-shot timer used for the success-pop delay */
static lv_timer_t* s_pop_timer  = nullptr;

/* Selected SSID persisted across step transitions */
static char s_sel_ssid[33]      = {0};

/* ── Forward declarations ────────────────────────────────────────────── */

static void show_step1_scan(lv_obj_t* scr);
static void show_step2_password(lv_obj_t* scr);
static void show_step3_connect(lv_obj_t* scr, const char* pw);

/* ── Topbar helper (replicates the exact settings.cpp topbar pattern) ── */

static void build_topbar(lv_obj_t* scr, const char* title_text,
                         lv_event_cb_t back_cb) {
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

    lv_obj_t* title = lv_label_create(topbar);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
}

/* ── Screen lifecycle ────────────────────────────────────────────────── */

static void screen_delete_cb(lv_event_t* /*e*/) {
    /* Stop any pending pop timer so it doesn't fire on freed memory */
    if (s_pop_timer) {
        lv_timer_delete(s_pop_timer);
        s_pop_timer = nullptr;
    }
    /* Null all widget statics — any in-flight callback touching LVGL
     * will null-check before acting (belt-and-suspenders) */
    s_screen     = nullptr;
    s_scan_cont  = nullptr;
    s_keyboard   = nullptr;
    s_textarea   = nullptr;
    s_status_lbl = nullptr;
    s_hint_lbl   = nullptr;
    LOG_I("wifi_setup", "screen_delete_cb: timers stopped, widget statics nulled");
}

/* ── Step-wide back callback ─────────────────────────────────────────── */

static void setup_back_cb(lv_event_t* /*e*/) {
    /* Cancel any pending one-shot pop timer before leaving, so it can't
     * double-pop or fire on freed memory after the screen is gone. */
    if (s_pop_timer) { lv_timer_delete(s_pop_timer); s_pop_timer = nullptr; }
    screen_router::pop();
    s_screen = nullptr;
}

/* ── Step 1 — Scan list ──────────────────────────────────────────────── */

/* Per-row user_data: points into file-static scan results buffer */
struct RowData {
    char    ssid[33];
    uint8_t enc;
};

/* Static scan-result storage (avoids heap allocation during the UI build) */
static wifi_profiles::ScanResult s_scan_buf[MAX_SCAN_RESULTS];
static uint8_t                   s_scan_count = 0;
/* One RowData per button row, pooled here for the screen lifetime */
static RowData                   s_row_data[MAX_SCAN_RESULTS];

static void ssid_btn_cb(lv_event_t* e) {
    RowData* rd = static_cast<RowData*>(lv_event_get_user_data(e));
    if (!rd || !s_screen) return;

    strlcpy(s_sel_ssid, rd->ssid, sizeof(s_sel_ssid));
    LOG_I("wifi_setup", "selected SSID (enc=%u)", rd->enc);  // no SSID in log

    if (rd->enc == 0) {
        /* Open network — skip password entry */
        show_step3_connect(s_screen, "");
    } else {
        show_step2_password(s_screen);
    }
}

static void show_step1_scan(lv_obj_t* scr) {
    /* Delete previous scan container if somehow re-entering Step 1 */
    if (s_scan_cont) {
        lv_obj_delete(s_scan_cont);
        s_scan_cont = nullptr;
    }

    /* Scrollable flex-column container below the topbar */
    lv_obj_t* cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LV_HOR_RES, LV_VER_RES - 36);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x0d0d1a), 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 6, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    s_scan_cont = cont;

    /* "Scanning…" placeholder */
    lv_obj_t* scanning_lbl = lv_label_create(cont);
    lv_label_set_text(scanning_lbl, "Scanning...");
    lv_obj_set_style_text_font(scanning_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(scanning_lbl, lv_color_hex(0x888899), 0);
    lv_obj_set_style_pad_top(scanning_lbl, 12, 0);

    /* Force LVGL to render the "Scanning…" label before we block on scan */
    lv_refr_now(NULL);

    /* Synchronous scan — blocks ~2-4 s, STA link may drop briefly */
    s_scan_count = wifi_profiles::scan(s_scan_buf, MAX_SCAN_RESULTS);
    LOG_I("wifi_setup", "scan complete: %u networks found", s_scan_count);

    /* Remove placeholder */
    lv_obj_delete(scanning_lbl);

    if (s_scan_count == 0) {
        lv_obj_t* none_lbl = lv_label_create(cont);
        lv_label_set_text(none_lbl, "No networks found.\nCheck WiFi antenna.");
        lv_obj_set_style_text_font(none_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(none_lbl, lv_color_hex(0x888899), 0);
        lv_obj_set_style_pad_top(none_lbl, 12, 0);
        return;
    }

    /* Build one lv_button row per scan result */
    for (uint8_t i = 0; i < s_scan_count; i++) {
        /* Populate per-row data pool */
        strlcpy(s_row_data[i].ssid, s_scan_buf[i].ssid, sizeof(s_row_data[i].ssid));
        s_row_data[i].enc = s_scan_buf[i].enc;

        lv_obj_t* btn = lv_button_create(cont);
        lv_obj_set_size(btn, LV_PCT(100), 40);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a1a2e), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a2a4e), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_margin_ver(btn, 2, 0);
        lv_obj_add_event_cb(btn, ssid_btn_cb, LV_EVENT_CLICKED,
                            static_cast<void*>(&s_row_data[i]));

        /* Row label: lock-or-wifi glyph + SSID + RSSI */
        char row_text[64];
        const char* lock_glyph = (s_scan_buf[i].enc != 0) ? LV_SYMBOL_CLOSE " " : LV_SYMBOL_WIFI " ";
        snprintf(row_text, sizeof(row_text), "%s%s  %d dBm",
                 lock_glyph, s_scan_buf[i].ssid, (int)s_scan_buf[i].rssi);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, row_text);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xddddff), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 6, 0);
    }
}

/* ── Step 2 — Password entry ─────────────────────────────────────────── */

static void keyboard_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        /* Checkmark pressed — grab password and proceed */
        if (!s_textarea || !s_screen) return;
        const char* pw = lv_textarea_get_text(s_textarea);
        /* Copy out the password before we delete the textarea widget */
        char pw_buf[65] = {0};
        strlcpy(pw_buf, pw ? pw : "", sizeof(pw_buf));
        show_step3_connect(s_screen, pw_buf);
    } else if (code == LV_EVENT_CANCEL) {
        /* Back arrow on keyboard — return to scan list.
         * Deleting the keyboard synchronously from inside its own event
         * dispatch causes a deferred-free race on the 48 KB heap.
         * Strategy: hide keyboard immediately, delete it async, then
         * rebuild step-1 via lv_async_call (runs after deferred free). */
        if (s_textarea) { lv_obj_delete(s_textarea); s_textarea = nullptr; }
        if (s_keyboard) {
            lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_delete_async(s_keyboard);
            s_keyboard = nullptr;
        }
        lv_async_call([](void*) { if (s_screen) show_step1_scan(s_screen); }, nullptr);
    }
}

static void show_step2_password(lv_obj_t* scr) {
    /* MEMORY: delete the scan container BEFORE creating the keyboard.
     * The keyboard is the heaviest LVGL widget; we must not hold both. */
    if (s_scan_cont) {
        lv_obj_delete(s_scan_cont);
        s_scan_cont = nullptr;
        LOG_I("wifi_setup", "step2: scan container freed before keyboard alloc");
    }

    /* Password textarea */
    lv_obj_t* ta = lv_textarea_create(scr);
    lv_obj_set_size(ta, LV_HOR_RES - 20, 40);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 44);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_placeholder_text(ta, "Enter password...");
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
    s_textarea = ta;

    /* On-screen keyboard */
    lv_obj_t* kb = lv_keyboard_create(scr);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_event_cb(kb, keyboard_event_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(kb, keyboard_event_cb, LV_EVENT_CANCEL, NULL);
    s_keyboard = kb;

    /* Heap sanity check — keyboard is the risk on 48 KB heap */
    uint32_t heap_after_kb = esp_get_free_heap_size();
    LOG_I("wifi_setup", "step2: heap after keyboard create = %lu bytes",
          (unsigned long)heap_after_kb);
    if (heap_after_kb < 25000) {
        LOG_W("wifi_setup", "step2: heap < 25 KB after keyboard — monitor for OOM");
    }
}

/* ── Step 3 — Connect + result ───────────────────────────────────────── */

static void pop_timer_cb(lv_timer_t* t) {
    /* Called once after SUCCESS_POP_DELAY_MS; timer auto-deletes itself
     * because we pass period=0 (one-shot). */
    (void)t;
    s_pop_timer = nullptr;   // null BEFORE pop — must not reference after screen teardown
    if (s_screen) {
        lv_obj_t* scr = s_screen;
        s_screen = nullptr;  // null before pop so back_cb can't double-cancel
        (void)scr;
        screen_router::pop();
    }
}

static void show_step3_connect(lv_obj_t* scr, const char* pw) {
    /* Clean up Step-2 widgets if still present (open-network path skips Step 2) */
    if (s_keyboard) { lv_obj_delete(s_keyboard); s_keyboard = nullptr; }
    if (s_textarea) { lv_obj_delete(s_textarea); s_textarea = nullptr; }
    /* Also clean up scan container if open-network path kept it */
    if (s_scan_cont) { lv_obj_delete(s_scan_cont); s_scan_cont = nullptr; }

    /* Delete stale status/hint labels from a previous Step-3 visit (re-entry
     * via open-network or retry) to prevent widget leak on 48 KB heap. */
    if (s_status_lbl) { lv_obj_delete(s_status_lbl); s_status_lbl = nullptr; }
    if (s_hint_lbl)   { lv_obj_delete(s_hint_lbl);   s_hint_lbl   = nullptr; }

    /* Status label */
    lv_obj_t* lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "Connecting...");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xddddff), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    s_status_lbl = lbl;

    /* Force UI update so "Connecting..." is visible before blocking */
    lv_refr_now(NULL);

    bool ok = wifi_profiles::connect_now(s_sel_ssid, pw, CONNECT_TIMEOUT_MS);

    if (!s_status_lbl) return;   /* screen was destroyed during connect */

    if (ok) {
        lv_label_set_text(s_status_lbl, "Connected " LV_SYMBOL_OK);
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x4ADE80), 0);
        LOG_I("wifi_setup", "step3: connected — scheduling pop in %u ms",
              (unsigned)SUCCESS_POP_DELAY_MS);

        /* One-shot timer: pop the screen after 1.5 s */
        s_pop_timer = lv_timer_create(pop_timer_cb, SUCCESS_POP_DELAY_MS, nullptr);
        lv_timer_set_repeat_count(s_pop_timer, 1);
    } else {
        lv_label_set_text(s_status_lbl, "Failed " LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xFF6B6B), 0);

        /* Sub-label: suggest trying again */
        lv_obj_t* hint = lv_label_create(scr);
        lv_label_set_text(hint, "Tap " LV_SYMBOL_LEFT " to retry");
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(0x888899), 0);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 30);
        s_hint_lbl = hint;  // track for cleanup on re-entry
        LOG_W("wifi_setup", "step3: connection failed for selected SSID");
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

lv_obj_t* create_screen() {
    /* Defensively cancel any stale one-shot timer left over from a prior session.
     * Normally screen_delete_cb cleans this up, but screen_router::pop() does NOT
     * auto-delete the outgoing screen, so LV_EVENT_DELETE may never have fired. */
    if (s_pop_timer) { lv_timer_delete(s_pop_timer); s_pop_timer = nullptr; }

    /* Reset all file-static widget pointers so callbacks from any prior session
     * cannot reference objects that no longer exist. */
    s_screen     = nullptr;
    s_scan_cont  = nullptr;
    s_keyboard   = nullptr;
    s_textarea   = nullptr;
    s_status_lbl = nullptr;
    s_hint_lbl   = nullptr;

    /* Reset SSID selection */
    s_sel_ssid[0] = '\0';

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d0d1a), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* Register delete handler — mirrors claude_widget.cpp lifecycle */
    lv_obj_add_event_cb(scr, screen_delete_cb, LV_EVENT_DELETE, NULL);

    s_screen = scr;

    /* Build the persistent topbar (Step 1 title; back navigates to caller) */
    build_topbar(scr, "Scan & Add Network", setup_back_cb);

    /* Kick off Step 1 */
    show_step1_scan(scr);

    return scr;
}

} // namespace wifi_setup

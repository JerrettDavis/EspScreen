#pragma once
#include <lvgl.h>

namespace widgets {

// ── Screen ────────────────────────────────────────────────────────────────

/** Create a bare screen object (320×480, BG_BASE, no padding). */
lv_obj_t* make_screen();

// ── Chrome bars ───────────────────────────────────────────────────────────

/**
 * Create a full-width topbar (BG_ELEVATED, TOPBAR_H tall).
 * If back_cb != nullptr a ghost back button is added aligned LEFT_MID.
 * The title label is always centred in the bar (unaffected by back btn).
 * @param out_title  If non-null, *out_title receives the title lv_obj_t*.
 * Returns the bar object.
 */
lv_obj_t* make_topbar(lv_obj_t* parent, const char* title,
                       lv_event_cb_t back_cb, lv_obj_t** out_title = nullptr);

/** Create a full-width bottom bar (BG_ELEVATED, BOTBAR_H tall, aligned BOTTOM_MID). */
lv_obj_t* make_botbar(lv_obj_t* parent);

// ── Content primitives ────────────────────────────────────────────────────

/**
 * Create a 130×100 app tile button (card style + card_pressed on press).
 * back_cb and user_data are wired to LV_EVENT_CLICKED internally.
 */
lv_obj_t* make_tile(lv_obj_t* parent, const char* label,
                    lv_event_cb_t cb, void* user_data = nullptr);

/**
 * Create a card container (SURFACE bg, flex COLUMN, pad SP_M, row-gap SP_S).
 * w <= 0  → LV_HOR_RES - 2*SCREEN_PAD
 * h <= 0  → LV_SIZE_CONTENT
 */
lv_obj_t* make_card(lv_obj_t* parent, int w = 0, int h = 0);

/**
 * Full-width tappable row (SURFACE bg, TAP_MIN height, flex ROW).
 * Left: label (style_text_value).  Right: optional value + trailing glyph (style_text_key).
 * If cb != nullptr the row is clickable and shows pressed style.
 * value / trailing may be nullptr (skipped).
 */
lv_obj_t* make_list_row(lv_obj_t* parent, const char* label, const char* value,
                         const char* trailing, lv_event_cb_t cb, void* user_data = nullptr);

/**
 * Thin key/value row (no background, LV_SIZE_CONTENT height).
 * Returns the VALUE label so the caller can update it.
 */
lv_obj_t* make_kv_row(lv_obj_t* parent, const char* key);

/** Section heading label (style_text_key, left-aligned). */
lv_obj_t* make_section_label(lv_obj_t* parent, const char* text);

/** 1px horizontal divider line (full content width, DIVIDER colour). */
lv_obj_t* make_divider(lv_obj_t* parent);

// ── Bar row ───────────────────────────────────────────────────────────────

struct BarRow {
    lv_obj_t* bar;
    lv_obj_t* pct;
    lv_obj_t* reset;
};

/**
 * Create a labelled progress-bar row inside parent.
 * Returns handles to the bar, pct-label, and reset sub-label for later mutation.
 */
BarRow make_bar_row(lv_obj_t* parent, const char* label);

/**
 * Update bar value and colour-code by threshold.
 *   pct <= 60  → SUCCESS (green)
 *   pct <= 85  → WARN    (amber)
 *   pct >  85  → ERROR_  (red)
 */
void bar_set(lv_obj_t* bar, lv_obj_t* pct_label, int pct);

// ── Misc ──────────────────────────────────────────────────────────────────

/** 12×12 circle dot, filled with runtime colour. */
lv_obj_t* make_status_dot(lv_obj_t* parent, uint32_t color);

} // namespace widgets

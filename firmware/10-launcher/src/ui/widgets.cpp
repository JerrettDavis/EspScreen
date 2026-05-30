#include "widgets.h"
#include "theme.h"
#include "tokens.h"
#include <lvgl.h>
#include <cstdio>

namespace widgets {

// ── make_screen ───────────────────────────────────────────────────────────

lv_obj_t* make_screen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_add_style(scr, ui_theme::style_screen(), LV_PART_MAIN);
    return scr;
}

// ── make_topbar ───────────────────────────────────────────────────────────

lv_obj_t* make_topbar(lv_obj_t* parent, const char* title,
                       lv_event_cb_t back_cb, lv_obj_t** out_title) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_HOR_RES, tok::TOPBAR_H);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(bar, ui_theme::style_bar_chrome(), LV_PART_MAIN);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    if (back_cb != nullptr) {
        lv_obj_t* btn = lv_button_create(bar);
        lv_obj_set_size(btn, tok::BACK_BTN_W, tok::BACK_BTN_H);
        lv_obj_align(btn, LV_ALIGN_LEFT_MID, tok::SP_S, 0);
        // Ghost style: transparent bg, accent border
        lv_obj_add_style(btn, ui_theme::style_btn_ghost(), LV_PART_MAIN);
        lv_obj_add_style(btn, ui_theme::style_btn_accent_pressed(),
                         LV_PART_MAIN | LV_STATE_PRESSED);

        lv_obj_t* arrow = lv_label_create(btn);
        lv_label_set_text(arrow, LV_SYMBOL_LEFT);
        lv_obj_center(arrow);

        lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t* lbl = lv_label_create(bar);
    lv_label_set_text(lbl, title);
    lv_obj_add_style(lbl, ui_theme::style_topbar_title(), LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    if (out_title != nullptr) {
        *out_title = lbl;
    }

    return bar;
}

// ── make_botbar ───────────────────────────────────────────────────────────

lv_obj_t* make_botbar(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_HOR_RES, tok::BOTBAR_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_style(bar, ui_theme::style_bar_chrome(), LV_PART_MAIN);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    return bar;
}

// ── make_tile ─────────────────────────────────────────────────────────────

lv_obj_t* make_tile(lv_obj_t* parent, const char* label,
                    lv_event_cb_t cb, void* user_data) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 130, 100);
    lv_obj_add_style(btn, ui_theme::style_card(), LV_PART_MAIN);
    lv_obj_add_style(btn, ui_theme::style_card_pressed(), LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_add_style(lbl, ui_theme::style_text_value(), LV_PART_MAIN);
    lv_obj_center(lbl);

    if (cb != nullptr) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }

    return btn;
}

// ── make_card ─────────────────────────────────────────────────────────────

lv_obj_t* make_card(lv_obj_t* parent, int w, int h) {
    lv_obj_t* card = lv_obj_create(parent);

    int actual_w = (w <= 0) ? (LV_HOR_RES - 2 * tok::SCREEN_PAD) : w;
    int actual_h = (h <= 0) ? LV_SIZE_CONTENT : h;
    lv_obj_set_size(card, actual_w, actual_h);

    lv_obj_add_style(card, ui_theme::style_card(), LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, tok::SP_S, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    return card;
}

// ── make_list_row ─────────────────────────────────────────────────────────

lv_obj_t* make_list_row(lv_obj_t* parent, const char* label, const char* value,
                         const char* trailing, lv_event_cb_t cb, void* user_data) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), tok::TAP_MIN);
    lv_obj_add_style(row, ui_theme::style_card(), LV_PART_MAIN);
    lv_obj_set_style_radius(row, tok::R_M, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, tok::SP_S, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    if (cb != nullptr) {
        lv_obj_add_style(row, ui_theme::style_card_pressed(), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user_data);
    } else {
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    }

    // Left label
    lv_obj_t* lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_add_style(lbl, ui_theme::style_text_value(), LV_PART_MAIN);
    lv_obj_set_flex_grow(lbl, 1); // push right-side items to the edge

    // Optional value label (right side, secondary colour)
    if (value != nullptr) {
        lv_obj_t* val = lv_label_create(row);
        lv_label_set_text(val, value);
        lv_obj_add_style(val, ui_theme::style_text_key(), LV_PART_MAIN);
    }

    // Optional trailing glyph (e.g. LV_SYMBOL_RIGHT)
    if (trailing != nullptr) {
        lv_obj_t* tr = lv_label_create(row);
        lv_label_set_text(tr, trailing);
        lv_obj_add_style(tr, ui_theme::style_text_key(), LV_PART_MAIN);
    }

    return row;
}

// ── make_kv_row ───────────────────────────────────────────────────────────

lv_obj_t* make_kv_row(lv_obj_t* parent, const char* key) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    lv_obj_t* key_lbl = lv_label_create(row);
    lv_label_set_text(key_lbl, key);
    lv_obj_add_style(key_lbl, ui_theme::style_text_key(), LV_PART_MAIN);

    lv_obj_t* val_lbl = lv_label_create(row);
    lv_label_set_text(val_lbl, "--");
    lv_obj_add_style(val_lbl, ui_theme::style_text_value(), LV_PART_MAIN);

    return val_lbl; // caller mutates this
}

// ── make_section_label ────────────────────────────────────────────────────

lv_obj_t* make_section_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_add_style(lbl, ui_theme::style_text_key(), LV_PART_MAIN);
    lv_obj_set_width(lbl, LV_PCT(100));
    return lbl;
}

// ── make_divider ──────────────────────────────────────────────────────────

lv_obj_t* make_divider(lv_obj_t* parent) {
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_set_size(line, LV_PCT(100), 1);
    lv_obj_add_style(line, ui_theme::style_divider(), LV_PART_MAIN);
    lv_obj_remove_flag(line, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return line;
}

// ── make_bar_row ──────────────────────────────────────────────────────────

BarRow make_bar_row(lv_obj_t* parent, const char* label) {
    // Outer column container (transparent, no padding)
    lv_obj_t* col = lv_obj_create(parent);
    lv_obj_set_size(col, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, tok::SP_XS, LV_PART_MAIN);
    lv_obj_remove_flag(col, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    // Row: label left, pct label right
    lv_obj_t* header = lv_obj_create(col);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(header, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    lv_obj_t* key_lbl = lv_label_create(header);
    lv_label_set_text(key_lbl, label);
    lv_obj_add_style(key_lbl, ui_theme::style_text_key(), LV_PART_MAIN);

    lv_obj_t* pct_lbl = lv_label_create(header);
    lv_label_set_text(pct_lbl, "0%");
    lv_obj_add_style(pct_lbl, ui_theme::style_title(), LV_PART_MAIN); // montserrat_20

    // Progress bar
    lv_obj_t* bar = lv_bar_create(col);
    lv_obj_set_size(bar, LV_PCT(100), 14);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(tok::BAR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(tok::SUCCESS), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, tok::R_S, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, tok::R_S, LV_PART_INDICATOR);

    // Reset / sub-label
    lv_obj_t* reset_lbl = lv_label_create(col);
    lv_label_set_text(reset_lbl, "");
    lv_obj_add_style(reset_lbl, ui_theme::style_text_muted(), LV_PART_MAIN);

    return BarRow{ bar, pct_lbl, reset_lbl };
}

// ── bar_set ───────────────────────────────────────────────────────────────

void bar_set(lv_obj_t* bar, lv_obj_t* pct_label, int pct) {
    if (!bar || !pct_label) return;
    // Threshold colour logic — matches claude_widget.cpp bar_color() semantics.
    // Note: claude_widget uses >=85 → red, >=60 → amber; we apply the same ordering.
    uint32_t color;
    if (pct > 85) {
        color = tok::ERROR_;
    } else if (pct > 60) {
        color = tok::WARN;
    } else {
        color = tok::SUCCESS;
    }

    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(pct_label, buf);
    lv_obj_set_style_text_color(pct_label, lv_color_hex(color), LV_PART_MAIN);
}

// ── make_status_dot ───────────────────────────────────────────────────────

lv_obj_t* make_status_dot(lv_obj_t* parent, uint32_t color) {
    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_remove_flag(dot, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return dot;
}

} // namespace widgets

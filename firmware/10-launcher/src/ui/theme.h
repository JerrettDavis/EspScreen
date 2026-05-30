#pragma once
#include <lvgl.h>

namespace ui_theme {

/**
 * Apply the EspScreen design-system theme to the default display.
 * Call once after display::init(). Safe to call multiple times (idempotent).
 */
void apply();

// ── Style accessors ──────────────────────────────────────────────────────
// All returned pointers are valid for the lifetime of the program.
// Apply with: lv_obj_add_style(obj, ui_theme::style_foo(), LV_PART_MAIN)
// For pressed variants use LV_PART_MAIN | LV_STATE_PRESSED as the selector.

/** Screen root: bg=BG_BASE, no border, no padding. */
lv_style_t* style_screen();

/** Card / container: bg=SURFACE, radius R_L, pad SP_M, no border. */
lv_style_t* style_card();

/** Card pressed state: bg=SURFACE_PRESS, shrinks 2px. */
lv_style_t* style_card_pressed();

/** Full-width chrome bar (topbar / botbar): bg=BG_ELEVATED, radius 0, pad 0. */
lv_style_t* style_bar_chrome();

/** Filled accent button (default state): bg=ACCENT, text=ACCENT_TEXT, radius R_M. */
lv_style_t* style_btn_accent();

/** Filled accent button pressed state: bg=ACCENT_PRESS, shrinks 2px. */
lv_style_t* style_btn_accent_pressed();

/** Ghost button: transparent bg, ACCENT_DIM border, ACCENT text, radius R_S. */
lv_style_t* style_btn_ghost();

/** Large heading text: montserrat_20, TEXT_PRIMARY. */
lv_style_t* style_title();

/** Topbar title text: montserrat_14, TEXT_PRIMARY. */
lv_style_t* style_topbar_title();

/** Secondary / key label: TEXT_SECOND. */
lv_style_t* style_text_key();

/** Primary value label: TEXT_PRIMARY. */
lv_style_t* style_text_value();

/** Muted / de-emphasised label: TEXT_MUTED. */
lv_style_t* style_text_muted();

/** 1px horizontal divider: bg=DIVIDER. */
lv_style_t* style_divider();

} // namespace ui_theme

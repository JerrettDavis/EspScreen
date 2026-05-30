#include "theme.h"
#include "tokens.h"
#include <lvgl.h>

// LV_FONT_MONTSERRAT_20 must be enabled in lv_conf.h
extern const lv_font_t lv_font_montserrat_14;
extern const lv_font_t lv_font_montserrat_20;

namespace ui_theme {

// ── File-static style storage ─────────────────────────────────────────────
// Must be static (program lifetime) — never stack-allocate lv_style_t objects
// that are left registered on widgets.

static lv_style_t s_screen;
static lv_style_t s_card;
static lv_style_t s_card_pressed;
static lv_style_t s_bar_chrome;
static lv_style_t s_btn_accent;
static lv_style_t s_btn_accent_pressed;
static lv_style_t s_btn_ghost;
static lv_style_t s_title;
static lv_style_t s_topbar_title;
static lv_style_t s_text_key;
static lv_style_t s_text_value;
static lv_style_t s_text_muted;
static lv_style_t s_divider;

static bool s_inited = false;

// ── apply() ───────────────────────────────────────────────────────────────

void apply() {
    lv_display_t* disp = lv_display_get_default();
    if (!disp) return;

    // Install LVGL default dark theme with cyan primary accent.
    lv_theme_t* th = lv_theme_default_init(
        disp,
        lv_color_hex(tok::ACCENT),   /* primary */
        lv_color_hex(tok::SUCCESS),  /* secondary */
        true,                         /* dark mode */
        &lv_font_montserrat_14
    );
    lv_display_set_theme(disp, th);

    if (s_inited) return;
    s_inited = true;

    // ── Screen ────────────────────────────────────────────────────────────
    lv_style_init(&s_screen);
    lv_style_set_bg_color(&s_screen, lv_color_hex(tok::BG_BASE));
    lv_style_set_bg_opa(&s_screen, LV_OPA_COVER);
    lv_style_set_border_width(&s_screen, 0);
    lv_style_set_pad_all(&s_screen, 0);
    lv_style_set_radius(&s_screen, 0);

    // ── Card ──────────────────────────────────────────────────────────────
    lv_style_init(&s_card);
    lv_style_set_bg_color(&s_card, lv_color_hex(tok::SURFACE));
    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_border_width(&s_card, 0);
    lv_style_set_pad_all(&s_card, tok::SP_M);
    lv_style_set_radius(&s_card, tok::R_L);

    // ── Card pressed ──────────────────────────────────────────────────────
    lv_style_init(&s_card_pressed);
    lv_style_set_bg_color(&s_card_pressed, lv_color_hex(tok::SURFACE_PRESS));
    lv_style_set_bg_opa(&s_card_pressed, LV_OPA_COVER);
    lv_style_set_transform_width(&s_card_pressed, -2);
    lv_style_set_transform_height(&s_card_pressed, -2);

    // ── Bar chrome (topbar / botbar) ──────────────────────────────────────
    lv_style_init(&s_bar_chrome);
    lv_style_set_bg_color(&s_bar_chrome, lv_color_hex(tok::BG_ELEVATED));
    lv_style_set_bg_opa(&s_bar_chrome, LV_OPA_COVER);
    lv_style_set_border_width(&s_bar_chrome, 0);
    lv_style_set_pad_all(&s_bar_chrome, 0);
    lv_style_set_radius(&s_bar_chrome, 0);

    // ── Accent button (default) ───────────────────────────────────────────
    lv_style_init(&s_btn_accent);
    lv_style_set_bg_color(&s_btn_accent, lv_color_hex(tok::ACCENT));
    lv_style_set_bg_opa(&s_btn_accent, LV_OPA_COVER);
    lv_style_set_text_color(&s_btn_accent, lv_color_hex(tok::ACCENT_TEXT));
    lv_style_set_border_width(&s_btn_accent, 0);
    lv_style_set_radius(&s_btn_accent, tok::R_M);

    // ── Accent button pressed ─────────────────────────────────────────────
    lv_style_init(&s_btn_accent_pressed);
    lv_style_set_bg_color(&s_btn_accent_pressed, lv_color_hex(tok::ACCENT_PRESS));
    lv_style_set_bg_opa(&s_btn_accent_pressed, LV_OPA_COVER);
    lv_style_set_transform_width(&s_btn_accent_pressed, -2);
    lv_style_set_transform_height(&s_btn_accent_pressed, -2);

    // ── Ghost button ──────────────────────────────────────────────────────
    lv_style_init(&s_btn_ghost);
    lv_style_set_bg_opa(&s_btn_ghost, LV_OPA_TRANSP);
    lv_style_set_border_color(&s_btn_ghost, lv_color_hex(tok::ACCENT_DIM));
    lv_style_set_border_width(&s_btn_ghost, 1);
    lv_style_set_border_opa(&s_btn_ghost, LV_OPA_COVER);
    lv_style_set_text_color(&s_btn_ghost, lv_color_hex(tok::ACCENT));
    lv_style_set_radius(&s_btn_ghost, tok::R_S);

    // ── Title (large heading) ─────────────────────────────────────────────
    lv_style_init(&s_title);
    lv_style_set_text_font(&s_title, &lv_font_montserrat_20);
    lv_style_set_text_color(&s_title, lv_color_hex(tok::TEXT_PRIMARY));

    // ── Topbar title ──────────────────────────────────────────────────────
    lv_style_init(&s_topbar_title);
    lv_style_set_text_font(&s_topbar_title, &lv_font_montserrat_14);
    lv_style_set_text_color(&s_topbar_title, lv_color_hex(tok::TEXT_PRIMARY));

    // ── Text key (secondary) ──────────────────────────────────────────────
    lv_style_init(&s_text_key);
    lv_style_set_text_font(&s_text_key, &lv_font_montserrat_14);
    lv_style_set_text_color(&s_text_key, lv_color_hex(tok::TEXT_SECOND));

    // ── Text value (primary) ──────────────────────────────────────────────
    lv_style_init(&s_text_value);
    lv_style_set_text_font(&s_text_value, &lv_font_montserrat_14);
    lv_style_set_text_color(&s_text_value, lv_color_hex(tok::TEXT_PRIMARY));

    // ── Text muted ────────────────────────────────────────────────────────
    lv_style_init(&s_text_muted);
    lv_style_set_text_font(&s_text_muted, &lv_font_montserrat_14);
    lv_style_set_text_color(&s_text_muted, lv_color_hex(tok::TEXT_MUTED));

    // ── Divider ───────────────────────────────────────────────────────────
    lv_style_init(&s_divider);
    lv_style_set_bg_color(&s_divider, lv_color_hex(tok::DIVIDER));
    lv_style_set_bg_opa(&s_divider, LV_OPA_COVER);
    lv_style_set_border_width(&s_divider, 0);
    lv_style_set_pad_all(&s_divider, 0);
    lv_style_set_radius(&s_divider, 0);
}

// ── Accessors ─────────────────────────────────────────────────────────────

lv_style_t* style_screen()             { return &s_screen; }
lv_style_t* style_card()               { return &s_card; }
lv_style_t* style_card_pressed()       { return &s_card_pressed; }
lv_style_t* style_bar_chrome()         { return &s_bar_chrome; }
lv_style_t* style_btn_accent()         { return &s_btn_accent; }
lv_style_t* style_btn_accent_pressed() { return &s_btn_accent_pressed; }
lv_style_t* style_btn_ghost()          { return &s_btn_ghost; }
lv_style_t* style_title()              { return &s_title; }
lv_style_t* style_topbar_title()       { return &s_topbar_title; }
lv_style_t* style_text_key()           { return &s_text_key; }
lv_style_t* style_text_value()         { return &s_text_value; }
lv_style_t* style_text_muted()         { return &s_text_muted; }
lv_style_t* style_divider()            { return &s_divider; }

} // namespace ui_theme

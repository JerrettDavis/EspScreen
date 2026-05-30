/*
 * calculator_stub.cpp  —  4-function calculator screen
 * Namespace:  calculator_stub (kept for app_registry linkage)
 * Display:    320×480 portrait, dark theme, LVGL 9.2.2
 *
 * Layout (px):
 *   0..39   topbar          (TOPBAR_H = 40)
 *  40..127  display panel   (88px)
 * 128..479  button matrix   (352px = 5 rows × ~62px + 4 × 10px gaps)
 *
 * Button map (4-column grid):
 *   Row 0:  C    +/-  %    /
 *   Row 1:  7    8    9    x
 *   Row 2:  4    5    6    -
 *   Row 3:  1    2    3    +
 *   Row 4:  0  (×2)  .    =
 *
 * All labels are plain ASCII — LVGL's stock Montserrat fonts only cover
 * Basic Latin + FontAwesome symbols; Unicode math glyphs (÷ × − ±) would
 * render as tofu rectangles on those fonts.
 */

#include "calculator_stub.h"
#include "../../os/screen_router.h"
#include "../../ui/widgets.h"
#include "../../ui/theme.h"
#include "../../ui/tokens.h"
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

// Font declared in theme.cpp / lv_conf.h — resolve at link time.
extern const lv_font_t lv_font_montserrat_20;

namespace calculator_stub {

// ─── Module-static widget pointer (nulled on unload) ─────────────────────────
static lv_obj_t* s_display = nullptr;   // the result label

// ─── Calculator state ─────────────────────────────────────────────────────────
static double   s_operand1   = 0.0;
static double   s_operand2   = 0.0;
static char     s_operator   = 0;       // '/', '*', '-', '+', or 0 = none
static bool     s_entering2  = false;   // true while typing second operand
static bool     s_just_eq    = false;   // true right after '=' computed result
static bool     s_error      = false;   // true when showing "Error"
static char     s_entry[32]  = "0";     // current display string (typed digits)

// ─── Button map ───────────────────────────────────────────────────────────────
// lv_buttonmatrix uses a flat array of strings; rows separated by "\n"; NULL terminates.
// ASCII-only labels — the LVGL Montserrat fonts cover only Basic Latin + FontAwesome
// symbols; Unicode math glyphs (÷ U+00F7, × U+00D7, − U+2212, ± U+00B1) fall outside
// that range and render as tofu rectangles.  Use ASCII stand-ins instead.
static const char* k_btnmap[] = {
    "C",  "+/-",  "%",  "/",  "\n",   // C  +/-  %  /
    "7",  "8",    "9",  "x",  "\n",   // 7  8    9  x
    "4",  "5",    "6",  "-",  "\n",   // 4  5    6  -
    "1",  "2",    "3",  "+",  "\n",   // 1  2    3  +
    "0",          ".",  "=",  ""      // 0 (wide)  .  =
};
// Row 4 btn indices (0-based flat count across all rows, no \n):
//   Row0: 0-3, Row1: 4-7, Row2: 8-11, Row3: 12-15, Row4: 16(0) 17(.) 18(=)
// "0" in row4 will be set to width=2 to span two columns.

// ─── Helpers ──────────────────────────────────────────────────────────────────

/** Trim trailing zeros after decimal point, but keep at least one digit. */
static void format_number(char* buf, size_t bufsz, double val) {
    // Check for integer value
    if (val == (long long)val && fabs(val) < 1e10) {
        snprintf(buf, bufsz, "%lld", (long long)val);
        return;
    }
    // Use up to 10 significant digits, then strip trailing zeros
    snprintf(buf, bufsz, "%.10g", val);
}

static void update_display() {
    if (!s_display) return;
    lv_label_set_text(s_display, s_entry);
}

static void reset_state() {
    s_operand1  = 0.0;
    s_operand2  = 0.0;
    s_operator  = 0;
    s_entering2 = false;
    s_just_eq   = false;
    s_error     = false;
    strncpy(s_entry, "0", sizeof(s_entry));
}

static void do_compute() {
    double result = 0.0;
    // Parse second operand from current entry
    s_operand2 = atof(s_entry);
    bool div_zero = false;
    switch (s_operator) {
        case '+': result = s_operand1 + s_operand2; break;
        case '-': result = s_operand1 - s_operand2; break;
        case '*': result = s_operand1 * s_operand2; break;
        case '/':
            if (s_operand2 == 0.0) { div_zero = true; break; }
            result = s_operand1 / s_operand2;
            break;
        default:  result = s_operand2; break;
    }
    if (div_zero) {
        strncpy(s_entry, "Error", sizeof(s_entry));
        s_error     = true;
        s_operator  = 0;
        s_entering2 = false;
        s_just_eq   = true;
        return;
    }
    format_number(s_entry, sizeof(s_entry), result);
    s_operand1  = result;
    s_operator  = 0;
    s_entering2 = false;
    s_just_eq   = true;
    s_error     = false;
}

// ─── Button matrix event handler ──────────────────────────────────────────────

static void btnmatrix_cb(lv_event_t* e) {
    lv_obj_t* btnm = (lv_obj_t*)lv_event_get_target(e);
    uint32_t  idx  = lv_buttonmatrix_get_selected_button(btnm);
    if (idx == LV_BUTTONMATRIX_BUTTON_NONE) return;

    const char* txt = lv_buttonmatrix_get_button_text(btnm, idx);
    if (!txt) return;

    // If previous state was error, any key except C resets first
    if (s_error && strcmp(txt, "C") != 0) {
        reset_state();
    }

    bool is_digit  = (txt[0] >= '0' && txt[0] <= '9' && txt[1] == '\0');
    bool is_dot    = (txt[0] == '.'  && txt[1] == '\0');

    // ── Clear ────────────────────────────────────────────────────────────────
    if (strcmp(txt, "C") == 0) {
        reset_state();

    // ── Digit ────────────────────────────────────────────────────────────────
    } else if (is_digit) {
        if (s_just_eq) {
            // Start fresh operand after result
            reset_state();
            s_just_eq = false;
        }
        if (!s_entering2 && s_operator != 0) {
            // First digit of second operand: clear entry
            strncpy(s_entry, txt, sizeof(s_entry));
            s_entering2 = true;
        } else {
            // Append digit (guard 31-char buffer and leading "0")
            size_t len = strlen(s_entry);
            if (strcmp(s_entry, "0") == 0 && txt[0] != '0') {
                // Replace leading zero
                strncpy(s_entry, txt, sizeof(s_entry));
            } else if (len < 12) {
                strncat(s_entry, txt, sizeof(s_entry) - len - 1);
            }
        }

    // ── Decimal point ────────────────────────────────────────────────────────
    } else if (is_dot) {
        if (s_just_eq) {
            reset_state();
            strncpy(s_entry, "0", sizeof(s_entry));
            s_just_eq = false;
        }
        if (!s_entering2 && s_operator != 0) {
            strncpy(s_entry, "0.", sizeof(s_entry));
            s_entering2 = true;
        } else {
            // Only add dot if not already present
            if (strchr(s_entry, '.') == nullptr) {
                size_t len = strlen(s_entry);
                if (len < 13) {
                    strncat(s_entry, ".", sizeof(s_entry) - len - 1);
                }
            }
        }

    // ── Equals ───────────────────────────────────────────────────────────────
    } else if (txt[0] == '=' && txt[1] == '\0') {
        if (s_operator != 0) {
            if (!s_entering2) {
                // No second operand typed: use operand1 as operand2 (repeat last)
                s_operand2 = s_operand1;
            }
            do_compute();
        }
        // If no operator, = is a no-op

    // ── Plus / Minus / Multiply / Divide (operator keys) ────────────────────
    } else {
        // Map display label → internal ASCII operator
        char op = 0;
        if (strcmp(txt, "+") == 0)     op = '+';
        else if (strcmp(txt, "-") == 0) op = '-';
        else if (strcmp(txt, "x") == 0) op = '*';
        else if (strcmp(txt, "/") == 0) op = '/';
        else if (strcmp(txt, "+/-") == 0) op = '~';  // toggle sign, handled below
        else if (strcmp(txt, "%") == 0) op = '%';    // percent, handled below

        if (op == '~') {
            // Toggle sign of current entry
            double val = atof(s_entry);
            val = -val;
            format_number(s_entry, sizeof(s_entry), val);
            if (s_entering2) {
                s_operand2 = val;
            } else {
                s_operand1 = val;
            }

        } else if (op == '%') {
            // Percent: divide current entry by 100
            double val = atof(s_entry);
            val /= 100.0;
            format_number(s_entry, sizeof(s_entry), val);
            if (s_entering2) {
                s_operand2 = val;
            } else {
                s_operand1 = val;
            }

        } else if (op != 0) {
            // Arithmetic operator
            if (s_operator != 0 && s_entering2) {
                // Chain: compute pending operation first
                do_compute();
            }
            // Latch operand1 and store operator
            s_operand1  = atof(s_entry);
            s_operator  = op;
            s_entering2 = false;
            s_just_eq   = false;
        }
    }

    update_display();
}

// ─── Screen unload handler ────────────────────────────────────────────────────

static void screen_unloaded_cb(lv_event_t* e) {
    s_display = nullptr;   // null static widget pointer before deletion
    screen_router::delete_on_unload(e);
}

static void back_cb(lv_event_t* e) {
    screen_router::pop();
}

// ─── create_screen() ─────────────────────────────────────────────────────────

lv_obj_t* create_screen() {
    // Always reset calculator state when screen is (re)opened
    reset_state();

    lv_obj_t* scr = widgets::make_screen();
    widgets::make_topbar(scr, "Calculator", back_cb);

    // Register unload handler (nulls s_display, then deletes screen)
    lv_obj_add_event_cb(scr, screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);

    // ── Display panel ─────────────────────────────────────────────────────────
    // Positioned immediately below topbar; right-aligned large number.
    // Height: 88px — generous for montserrat_20 + padding.
    const int DISP_Y  = tok::TOPBAR_H;
    const int DISP_H  = 88;
    const int BTN_Y   = DISP_Y + DISP_H;                    // 128
    const int BTN_H   = LV_VER_RES - BTN_Y;                 // 480-128 = 352
    const int PAD     = tok::SP_S;                           // 8px inner padding

    lv_obj_t* disp_panel = lv_obj_create(scr);
    lv_obj_set_size(disp_panel, LV_HOR_RES, DISP_H);
    lv_obj_set_pos(disp_panel, 0, DISP_Y);
    lv_obj_set_style_bg_color(disp_panel, lv_color_hex(tok::BG_ELEVATED), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(disp_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(disp_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(disp_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(disp_panel, PAD, LV_PART_MAIN);
    lv_obj_remove_flag(disp_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Result label — right-aligned, montserrat_20 (largest built-in font)
    s_display = lv_label_create(disp_panel);
    lv_label_set_text(s_display, s_entry);
    lv_obj_add_style(s_display, ui_theme::style_title(), LV_PART_MAIN);
    lv_obj_set_width(s_display, LV_PCT(100));
    lv_obj_set_style_text_align(s_display, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_long_mode(s_display, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_display, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    // Thin divider between display and buttons
    lv_obj_t* div = lv_obj_create(scr);
    lv_obj_set_size(div, LV_HOR_RES, 1);
    lv_obj_set_pos(div, 0, BTN_Y - 1);
    lv_obj_set_style_bg_color(div, lv_color_hex(tok::DIVIDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(div, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(div, 0, LV_PART_MAIN);
    lv_obj_remove_flag(div, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    // ── Button matrix ─────────────────────────────────────────────────────────
    lv_obj_t* btnm = lv_buttonmatrix_create(scr);
    lv_obj_set_size(btnm, LV_HOR_RES, BTN_H);
    lv_obj_set_pos(btnm, 0, BTN_Y);
    lv_buttonmatrix_set_map(btnm, k_btnmap);

    // "0" button (index 16 in 0-based flat index) spans 2 units
    lv_buttonmatrix_set_button_width(btnm, 16, 2);

    // Style the button matrix background (transparent — buttons provide colour)
    lv_obj_set_style_bg_color(btnm, lv_color_hex(tok::BG_BASE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btnm, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btnm, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btnm, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(btnm, 2, LV_PART_MAIN);  // 2px gap between buttons

    // Default button appearance (SURFACE background, primary text)
    lv_obj_set_style_bg_color(btnm, lv_color_hex(tok::SURFACE), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(btnm, lv_color_hex(tok::TEXT_PRIMARY), LV_PART_ITEMS);
    lv_obj_set_style_text_font(btnm, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_radius(btnm, tok::R_M, LV_PART_ITEMS);
    lv_obj_set_style_border_width(btnm, 0, LV_PART_ITEMS);

    // Pressed state: SURFACE_PRESS
    lv_obj_set_style_bg_color(btnm, lv_color_hex(tok::SURFACE_PRESS),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_COVER,
                             LV_PART_ITEMS | LV_STATE_PRESSED);

    // ── Per-button accent colouring ───────────────────────────────────────────
    // LV_BUTTONMATRIX_CTRL_CHECKED (0x0100) makes a button render with
    // LV_STATE_CHECKED, so styling LV_PART_ITEMS|LV_STATE_CHECKED targets only
    // those buttons.  Tag the right-column operators and "=".
    //
    // Flat button index map (\n entries NOT counted):
    //   Row 0: C=0  ±=1  %=2  ÷=3
    //   Row 1: 7=4  8=5  9=6  ×=7
    //   Row 2: 4=8  5=9  6=10 −=11
    //   Row 3: 1=12 2=13 3=14 +=15
    //   Row 4: 0=16 .=17 ==18
    const uint32_t accent_btns[] = { 3, 7, 11, 15, 18 };
    for (size_t i = 0; i < 5; i++) {
        lv_buttonmatrix_set_button_ctrl(btnm, accent_btns[i],
                                        LV_BUTTONMATRIX_CTRL_CHECKED);
    }
    // Accent style for CHECKED buttons
    lv_obj_set_style_bg_color(btnm, lv_color_hex(tok::ACCENT),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_COVER,
                             LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btnm, lv_color_hex(tok::ACCENT_TEXT),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    // Pressed accent: slightly darker
    lv_obj_set_style_bg_color(btnm, lv_color_hex(tok::ACCENT_PRESS),
                              LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_COVER,
                             LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_PRESSED);

    // Register event handler
    lv_obj_add_event_cb(btnm, btnmatrix_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return scr;
}

} // namespace calculator_stub

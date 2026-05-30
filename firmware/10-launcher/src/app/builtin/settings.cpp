/**
 * settings.cpp — Settings screen with Claude Profiles and WiFi Networks tiles.
 *
 * v2: Migrated to shared UI component library (widgets / tokens / theme).
 *     Edit/Add/Delete operations use serial commands.
 */

#include "settings.h"
#include "wifi_setup.h"
#include "../../os/screen_router.h"
#include "../../os/claude_auth.h"
#include "../../os/wifi_profiles.h"
#include "../../ui/widgets.h"
#include "../../ui/theme.h"
#include "../../ui/tokens.h"
#include "../../os/logger.h"
#if __has_include("os/net_manager.h")
#  include "os/net_manager.h"
#endif
#include <lvgl.h>
#include <Arduino.h>

namespace settings {

/* ── Shared sub-screen cleanup ──────────────────────────────────────────── */
/* Fires on the outgoing screen AFTER the fade-out animation completes.
 * Deleting here (deferred) avoids the use-after-free that occurs when
 * lv_obj_delete() is called before screen_router::pop() starts the fade. */
static void subscreen_unloaded_cb(lv_event_t* e) {
    lv_obj_t* scr = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_delete_async(scr);   // safe: animation is already finished
}

/* ── Claude Profiles sub-screen ─────────────────────────────────────────── */

static lv_obj_t* s_claude_screen = nullptr;

static void claude_profiles_back_cb(lv_event_t* /*e*/) {
    /* Null the pointer now; actual deletion happens in subscreen_unloaded_cb
     * after the fade-out animation finishes — avoids use-after-free hang. */
    s_claude_screen = nullptr;
    screen_router::pop();
}

static lv_obj_t* create_claude_profiles_screen() {
    lv_obj_t* scr = widgets::make_screen();
    lv_obj_add_event_cb(scr, subscreen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);

    widgets::make_topbar(scr, "Claude Profiles", claude_profiles_back_cb);

    /* Content container — flex COLUMN, below topbar */
    lv_obj_t* content = lv_obj_create(scr);
    lv_obj_set_size(content, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_pos(content, 0, tok::TOPBAR_H);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, tok::SCREEN_PAD, 0);
    lv_obj_set_style_pad_row(content, tok::SP_M, 0);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    uint8_t count = claude_auth::profile_count();
    uint8_t active = claude_auth::active_index();

    if (count == 0) {
        lv_obj_t* lbl = lv_label_create(content);
        lv_label_set_text(lbl, "No profiles configured.");
        lv_obj_add_style(lbl, ui_theme::style_text_muted(), LV_PART_MAIN);
    } else {
        for (uint8_t i = 0; i < count && i < claude_auth::MAX_PROFILES; i++) {
            claude_auth::Profile p;
            if (!claude_auth::load_profile(i, p)) continue;

            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "%u: %s", i, p.label);

            lv_obj_t* row = widgets::make_list_row(content, row_buf,
                                                   nullptr, nullptr,
                                                   nullptr, nullptr);

            if (i == active) {
                /* Status dot for active profile */
                widgets::make_status_dot(row, tok::SUCCESS);
                /* Color the label SUCCESS */
                lv_obj_t* row_lbl = lv_obj_get_child(row, 0);
                if (row_lbl) {
                    lv_obj_set_style_text_color(row_lbl,
                        lv_color_hex(tok::SUCCESS), LV_PART_MAIN);
                }
            }
        }
    }

    /* Serial hint at the bottom */
    lv_obj_t* hint = lv_label_create(content);
    lv_label_set_text(hint,
        "Use serial to manage profiles:\n"
        "  claude profile add \"Name\"\n"
        "  claude profile list\n"
        "  claude profile use \"Name\"\n"
        "  claude token set \"Name\" <access> <refresh> <expires_sec>");
    lv_obj_add_style(hint, ui_theme::style_text_muted(), LV_PART_MAIN);
    lv_obj_set_style_text_line_space(hint, 4, 0);

    s_claude_screen = scr;
    return scr;
}

/* ── WiFi Networks sub-screen ────────────────────────────────────────────── */

static lv_obj_t* s_wifi_screen = nullptr;

static void wifi_networks_back_cb(lv_event_t* /*e*/) {
    /* Null the pointer now; actual deletion happens in subscreen_unloaded_cb
     * after the fade-out animation finishes — avoids use-after-free hang. */
    s_wifi_screen = nullptr;
    screen_router::pop();
}

static lv_obj_t* create_wifi_networks_screen() {
    lv_obj_t* scr = widgets::make_screen();
    lv_obj_add_event_cb(scr, subscreen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);

    widgets::make_topbar(scr, "WiFi Networks", wifi_networks_back_cb);

    /* Content container — flex COLUMN, below topbar */
    lv_obj_t* content = lv_obj_create(scr);
    lv_obj_set_size(content, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_pos(content, 0, tok::TOPBAR_H);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, tok::SCREEN_PAD, 0);
    lv_obj_set_style_pad_row(content, tok::SP_M, 0);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    uint8_t count = wifi_profiles::network_count();
    String connected_ssid = wifi_profiles::get_ssid();

    if (count == 0) {
        lv_obj_t* lbl = lv_label_create(content);
        lv_label_set_text(lbl, "No networks configured.");
        lv_obj_add_style(lbl, ui_theme::style_text_muted(), LV_PART_MAIN);
    } else {
        for (uint8_t i = 0; i < count && i < wifi_profiles::MAX_NETWORKS; i++) {
            wifi_profiles::Network net;
            if (!wifi_profiles::load_network(i, net)) continue;

            bool is_current = connected_ssid.equalsIgnoreCase(net.ssid) &&
                              wifi_profiles::is_connected();

            char label_buf[64];
            snprintf(label_buf, sizeof(label_buf), "%u: %s", i, net.ssid);

            const char* trailing = is_current ? "[connected]" : nullptr;

            lv_obj_t* row = widgets::make_list_row(content, label_buf,
                                                   nullptr, trailing,
                                                   nullptr, nullptr);

            if (is_current) {
                /* Color label SUCCESS */
                lv_obj_t* row_lbl = lv_obj_get_child(row, 0);
                if (row_lbl) {
                    lv_obj_set_style_text_color(row_lbl,
                        lv_color_hex(tok::SUCCESS), LV_PART_MAIN);
                }
                /* Style "[connected]" trailing as text_key — must happen BEFORE
                 * make_status_dot() appends the dot, otherwise child(-1) returns
                 * the dot instead of the trailing label. */
                lv_obj_t* trail_lbl = lv_obj_get_child(row, -1);
                if (trail_lbl && trail_lbl != row_lbl) {
                    lv_obj_add_style(trail_lbl, ui_theme::style_text_key(), LV_PART_MAIN);
                }
                /* Trailing status dot — appended last so child(-1) is the dot */
                widgets::make_status_dot(row, tok::SUCCESS);
            }
        }
    }

    /* "Scan & Add Network" accent button */
    lv_obj_t* scan_btn = lv_button_create(content);
    lv_obj_set_width(scan_btn, LV_HOR_RES - 2 * tok::SCREEN_PAD);
    lv_obj_set_height(scan_btn, tok::TAP_MIN);
    lv_obj_set_style_radius(scan_btn, tok::R_M, LV_PART_MAIN);
    lv_obj_set_style_border_width(scan_btn, 0, LV_PART_MAIN);
    lv_obj_add_style(scan_btn, ui_theme::style_btn_accent(), LV_PART_MAIN);
    lv_obj_add_style(scan_btn, ui_theme::style_btn_accent_pressed(), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(scan_btn, [](lv_event_t* /*e*/) {
        screen_router::push(wifi_setup::create_screen());
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_WIFI " Scan & Add Network");
    lv_obj_center(scan_lbl);

    /* Serial hint */
    lv_obj_t* hint = lv_label_create(content);
    lv_label_set_text(hint, "Serial: wifi add/list/prefer/remove/clear");
    lv_obj_add_style(hint, ui_theme::style_text_muted(), LV_PART_MAIN);

    s_wifi_screen = scr;
    return scr;
}

/* ── Tile click callbacks ────────────────────────────────────────────────── */

static void claude_tile_cb(lv_event_t* /*e*/) {
    screen_router::push(create_claude_profiles_screen());
}

static void wifi_tile_cb(lv_event_t* /*e*/) {
    screen_router::push(create_wifi_networks_screen());
}

static void settings_back_cb(lv_event_t* /*e*/) {
    screen_router::pop();
}

/* ── Main settings screen ────────────────────────────────────────────────── */

lv_obj_t* create_screen() {
    lv_obj_t* scr = widgets::make_screen();

    widgets::make_topbar(scr, "Settings", settings_back_cb);

    /* Content container — flex COLUMN, starts just below topbar */
    lv_obj_t* content = lv_obj_create(scr);
    lv_obj_set_size(content, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_pos(content, 0, tok::TOPBAR_H);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, tok::SCREEN_PAD, 0);
    lv_obj_set_style_pad_row(content, tok::SP_M, 0);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* 1. Claude Profiles row */
    uint8_t p_count = claude_auth::profile_count();
    char claude_buf[32];
    snprintf(claude_buf, sizeof(claude_buf), "(%u/%u)",
             p_count, (uint8_t)claude_auth::MAX_PROFILES);
    widgets::make_list_row(content, "Claude Profiles", claude_buf,
                           LV_SYMBOL_RIGHT, claude_tile_cb, nullptr);

    /* 2. WiFi Networks row */
    uint8_t n_count = wifi_profiles::network_count();
    char wifi_buf[48];
    snprintf(wifi_buf, sizeof(wifi_buf), "(%u/%u)%s",
             n_count, (uint8_t)wifi_profiles::MAX_NETWORKS,
             wifi_profiles::is_connected() ? "  \xE2\x97\x8F" : "");
    widgets::make_list_row(content, "WiFi Networks", wifi_buf,
                           LV_SYMBOL_RIGHT, wifi_tile_cb, nullptr);

    /* 3. Divider */
    widgets::make_divider(content);

    /* 4. Network status */
    lv_obj_t* net_status_lbl = lv_label_create(content);

#if __has_include("os/net_manager.h")
    /* net_manager.h available — show rich mode + optional AP SSID */
    {
        char net_buf[64] = {0};
        net_manager::Mode m = net_manager::mode();
        switch (m) {
            case net_manager::Mode::Boot:
                snprintf(net_buf, sizeof(net_buf), "Net: Booting...");
                break;
            case net_manager::Mode::StaConnecting:
                snprintf(net_buf, sizeof(net_buf), "Net: Connecting...");
                break;
            case net_manager::Mode::StaConnected:
                snprintf(net_buf, sizeof(net_buf), "Net: Connected (STA)");
                break;
            case net_manager::Mode::ApPortal:
                snprintf(net_buf, sizeof(net_buf), "Net: AP Portal  %s",
                         net_manager::ap_ssid() ? net_manager::ap_ssid() : "");
                break;
            case net_manager::Mode::ApStaRetry:
                snprintf(net_buf, sizeof(net_buf), "Net: AP+STA retry");
                break;
            default:
                snprintf(net_buf, sizeof(net_buf), "Net: Unknown");
                break;
        }
        lv_label_set_text(net_status_lbl, net_buf);
        lv_obj_set_style_text_color(net_status_lbl,
            (m == net_manager::Mode::StaConnected)
                ? lv_color_hex(tok::SUCCESS)
                : lv_color_hex(tok::TEXT_MUTED), LV_PART_MAIN);
    }
#else
    /* Fallback: simple connected / not-connected from wifi_profiles */
    lv_label_set_text(net_status_lbl,
        wifi_profiles::is_connected() ? "Net: Connected" : "Net: Not connected");
    lv_obj_set_style_text_color(net_status_lbl,
        wifi_profiles::is_connected()
            ? lv_color_hex(tok::SUCCESS)
            : lv_color_hex(tok::TEXT_MUTED), LV_PART_MAIN);
#endif

    /* 5. Info hint */
    lv_obj_t* info_lbl = lv_label_create(content);
    lv_label_set_text(info_lbl, "Touch: factory cal (Phase 0)");
    lv_obj_add_style(info_lbl, ui_theme::style_text_muted(), LV_PART_MAIN);

    return scr;
}

} // namespace settings

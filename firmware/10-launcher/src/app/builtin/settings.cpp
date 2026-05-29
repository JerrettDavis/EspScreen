/**
 * settings.cpp — Settings screen with Claude Profiles and WiFi Networks tiles.
 *
 * v1: Edit/Add/Delete operations use serial commands.
 *     These tiles show current state and instruct user to use serial for changes.
 */

#include "settings.h"
#include "wifi_setup.h"
#include "../../os/screen_router.h"
#include "../../os/claude_auth.h"
#include "../../os/wifi_profiles.h"
#include "../../ui/widgets.h"
#include "../../os/logger.h"
#if __has_include("os/net_manager.h")
#  include "os/net_manager.h"
#endif
#include <lvgl.h>
#include <Arduino.h>

namespace settings {

/* ── Back callback ──────────────────────────────────────────────────── */
static void back_cb(lv_event_t* /*e*/) {
    screen_router::pop();
}

/* ── Claude Profiles sub-screen ─────────────────────────────────────── */

static lv_obj_t* s_claude_screen = nullptr;

static void claude_profiles_back_cb(lv_event_t* /*e*/) {
    /* Delete the sub-screen before popping so it doesn't leak on the 48 KB heap.
     * screen_router::pop() restores the previous screen but does NOT auto-delete
     * the outgoing one. */
    if (s_claude_screen) { lv_obj_delete(s_claude_screen); s_claude_screen = nullptr; }
    screen_router::pop();
}

static lv_obj_t* create_claude_profiles_screen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d0d1a), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* Top bar */
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
    lv_obj_add_event_cb(back_btn, claude_profiles_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    lv_obj_t* title = lv_label_create(topbar);
    lv_label_set_text(title, "Claude Profiles");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* Profile list */
    lv_coord_t y = 44;
    uint8_t count = claude_auth::profile_count();
    uint8_t active = claude_auth::active_index();

    if (count == 0) {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, "No profiles configured.");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x888899), 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);
        y += 20;
    } else {
        for (uint8_t i = 0; i < count && i < claude_auth::MAX_PROFILES; i++) {
            claude_auth::Profile p;
            if (!claude_auth::load_profile(i, p)) continue;

            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "%s%u: %s",
                     (i == active) ? "* " : "  ", i, p.label);

            lv_obj_t* row_lbl = lv_label_create(scr);
            lv_label_set_text(row_lbl, row_buf);
            lv_obj_set_style_text_font(row_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(row_lbl,
                (i == active) ? lv_color_hex(0x4ADE80) : lv_color_hex(0xddddff), 0);
            lv_obj_align(row_lbl, LV_ALIGN_TOP_LEFT, 15, y);
            y += 20;
        }
    }

    y += 10;
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint,
        "Use serial to manage profiles:\n"
        "  claude profile add \"Name\"\n"
        "  claude profile list\n"
        "  claude profile use \"Name\"\n"
        "  claude token set \"Name\" <access> <refresh> <expires_sec>");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888899), 0);
    lv_obj_set_style_text_line_space(hint, 4, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 15, y);

    s_claude_screen = scr;
    return scr;
}

/* ── WiFi Networks sub-screen ────────────────────────────────────────── */

static lv_obj_t* s_wifi_screen = nullptr;

static void wifi_networks_back_cb(lv_event_t* /*e*/) {
    /* Delete the sub-screen before popping to avoid leaking it on the 48 KB heap. */
    if (s_wifi_screen) { lv_obj_delete(s_wifi_screen); s_wifi_screen = nullptr; }
    screen_router::pop();
}

static lv_obj_t* create_wifi_networks_screen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d0d1a), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* Top bar */
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
    lv_obj_add_event_cb(back_btn, wifi_networks_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    lv_obj_t* title = lv_label_create(topbar);
    lv_label_set_text(title, "WiFi Networks");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* Network list */
    lv_coord_t y = 44;
    uint8_t count = wifi_profiles::network_count();
    String connected_ssid = wifi_profiles::get_ssid();

    if (count == 0) {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, "No networks configured.");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x888899), 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);
        y += 20;
    } else {
        for (uint8_t i = 0; i < count && i < wifi_profiles::MAX_NETWORKS; i++) {
            wifi_profiles::Network net;
            if (!wifi_profiles::load_network(i, net)) continue;

            bool is_current = connected_ssid.equalsIgnoreCase(net.ssid) &&
                              wifi_profiles::is_connected();
            char row_buf[80];
            snprintf(row_buf, sizeof(row_buf), "%s%u: %s (prio=%u)%s",
                     is_current ? "* " : "  ", i, net.ssid, net.prio,
                     is_current ? " [connected]" : "");

            lv_obj_t* row_lbl = lv_label_create(scr);
            lv_label_set_text(row_lbl, row_buf);
            lv_obj_set_style_text_font(row_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(row_lbl,
                is_current ? lv_color_hex(0x4ADE80) : lv_color_hex(0xddddff), 0);
            lv_obj_align(row_lbl, LV_ALIGN_TOP_LEFT, 15, y);
            y += 20;
        }
    }

    y += 14;
    /* "Scan & Add Network" button — launches the on-screen WiFi setup flow */
    lv_obj_t* scan_btn = lv_button_create(scr);
    lv_obj_set_size(scan_btn, 240, 40);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x1a3a1a), 0);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x2a5a2a), LV_STATE_PRESSED);
    lv_obj_set_style_radius(scan_btn, 8, 0);
    lv_obj_set_style_border_width(scan_btn, 0, 0);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_event_cb(scan_btn, [](lv_event_t* /*e*/) {
        screen_router::push(wifi_setup::create_screen());
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_WIFI "  Scan & Add Network");
    lv_obj_set_style_text_font(scan_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(scan_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(scan_lbl);

    /* Serial hint retained below the button */
    y += 50;
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint,
        "Serial: wifi add/list/prefer/remove/clear");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666677), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 15, y);

    s_wifi_screen = scr;
    return scr;
}

/* ── Tile click callbacks ────────────────────────────────────────────── */

static void claude_tile_cb(lv_event_t* /*e*/) {
    screen_router::push(create_claude_profiles_screen());
}

static void wifi_tile_cb(lv_event_t* /*e*/) {
    screen_router::push(create_wifi_networks_screen());
}

/* ── Main settings screen ────────────────────────────────────────────── */

lv_obj_t* create_screen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* Info row */
    lv_obj_t* info_lbl = lv_label_create(scr);
    lv_label_set_text(info_lbl, "Touch: factory cal (Phase 0)");
    lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(info_lbl, LV_ALIGN_TOP_MID, 0, 48);

    /* Claude Profiles tile */
    lv_obj_t* claude_tile = lv_button_create(scr);
    lv_obj_set_size(claude_tile, 260, 48);
    lv_obj_align(claude_tile, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_color(claude_tile, lv_color_hex(0x2a2a4e), 0);
    lv_obj_set_style_radius(claude_tile, 8, 0);
    lv_obj_add_event_cb(claude_tile, claude_tile_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* claude_lbl = lv_label_create(claude_tile);
    uint8_t p_count = claude_auth::profile_count();
    char claude_buf[48];
    snprintf(claude_buf, sizeof(claude_buf), "Claude Profiles  (%u/%u)",
             p_count, (uint8_t)claude_auth::MAX_PROFILES);
    lv_label_set_text(claude_lbl, claude_buf);
    lv_obj_set_style_text_font(claude_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(claude_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(claude_lbl);

    /* WiFi Networks tile */
    lv_obj_t* wifi_tile = lv_button_create(scr);
    lv_obj_set_size(wifi_tile, 260, 48);
    lv_obj_align(wifi_tile, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_set_style_bg_color(wifi_tile, lv_color_hex(0x2a4e2a), 0);
    lv_obj_set_style_radius(wifi_tile, 8, 0);
    lv_obj_add_event_cb(wifi_tile, wifi_tile_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* wifi_lbl = lv_label_create(wifi_tile);
    uint8_t n_count = wifi_profiles::network_count();
    char wifi_buf[48];
    snprintf(wifi_buf, sizeof(wifi_buf), "WiFi Networks  (%u/%u)%s",
             n_count, (uint8_t)wifi_profiles::MAX_NETWORKS,
             wifi_profiles::is_connected() ? "  *" : "");
    lv_label_set_text(wifi_lbl, wifi_buf);
    lv_obj_set_style_text_font(wifi_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wifi_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(wifi_lbl);

    /* Network connectivity status label */
    lv_obj_t* net_status_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(net_status_lbl, &lv_font_montserrat_14, 0);

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
                ? lv_color_hex(0x4ADE80)
                : lv_color_hex(0x888899), 0);
    }
#else
    /* Fallback: simple connected / not-connected from wifi_profiles */
    lv_label_set_text(net_status_lbl,
        wifi_profiles::is_connected() ? "Net: Connected" : "Net: Not connected");
    lv_obj_set_style_text_color(net_status_lbl,
        wifi_profiles::is_connected()
            ? lv_color_hex(0x4ADE80)
            : lv_color_hex(0x888899), 0);
#endif

    lv_obj_align(net_status_lbl, LV_ALIGN_TOP_MID, 0, 200);

    widgets::make_back_btn(scr, back_cb);
    return scr;
}

} // namespace settings

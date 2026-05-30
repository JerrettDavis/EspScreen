#include "launcher.h"
#include "../../app/app_registry.h"
#include "../../os/screen_router.h"
#include "../../os/wifi_profiles.h"
#include "../../ui/widgets.h"
#include "../../ui/theme.h"
#include "../../ui/tokens.h"
#include <lvgl.h>
#include <Arduino.h>

namespace launcher {

static void tile_click_cb(lv_event_t* e) {
    const AppEntry* entry = (const AppEntry*)lv_event_get_user_data(e);
    if (entry && entry->create_screen) {
        screen_router::push(entry->create_screen());
    }
}

lv_obj_t* create_screen() {
    Serial.println("[launcher] create_screen() entered");

    /* Create dark BG_BASE screen (320x480) */
    lv_obj_t* scr = widgets::make_screen();

    /* Title row: label + status dot */
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "EspScreen");
    lv_obj_add_style(title, ui_theme::style_title(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, tok::SP_M);

    /* Status dot: green (SUCCESS) if connected, else red (ERROR_) */
    uint32_t dot_color = wifi_profiles::is_connected() ? tok::SUCCESS : tok::ERROR_;
    lv_obj_t* dot = widgets::make_status_dot(scr, dot_color);
    /* Position dot to the right of title; offset ~30px from title center */
    lv_obj_align(dot, LV_ALIGN_TOP_MID, 85, tok::SP_M);

    /* Flex grid: ROW_WRAP, 300px wide, centered TOP_MID */
    lv_obj_t* grid = lv_obj_create(scr);
    lv_obj_set_size(grid, 300, LV_VER_RES - 60);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_opa(grid, LV_OPA_0, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, tok::SP_S, 0);
    lv_obj_set_style_pad_gap(grid, tok::SP_M, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);

    /* Create tiles from registry, wiring click callbacks internally */
    for (int i = 0; i < app_registry::kBuiltinAppCount; i++) {
        const AppEntry* entry = &app_registry::kBuiltinApps[i];
        /* make_tile now wires the callback internally (no separate lv_obj_add_event_cb needed) */
        widgets::make_tile(grid, entry->icon, entry->label, tile_click_cb, (void*)entry);
    }

    Serial.printf("[launcher] create_screen() returning scr=%p\n", (void*)scr);
    return scr;
}

} // namespace launcher

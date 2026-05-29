#include "launcher.h"
#include "../../app/app_registry.h"
#include "../../os/screen_router.h"
#include "../../ui/widgets.h"
#include <lvgl.h>

namespace launcher {

static void tile_click_cb(lv_event_t* e) {
    const AppEntry* entry = (const AppEntry*)lv_event_get_user_data(e);
    if (entry && entry->create_screen) {
        screen_router::push(entry->create_screen());
    }
}

lv_obj_t* create_screen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    /* Title label */
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "EspScreen");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* 2-column flex grid */
    lv_obj_t* grid = lv_obj_create(scr);
    lv_obj_set_size(grid, 300, LV_VER_RES - 60);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_bg_opa(grid, LV_OPA_0, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 8, 0);
    lv_obj_set_style_pad_gap(grid, 10, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);

    for (int i = 0; i < app_registry::kBuiltinAppCount; i++) {
        const AppEntry* entry = &app_registry::kBuiltinApps[i];
        lv_obj_t* tile = widgets::make_tile(grid, entry->label, tile_click_cb);
        lv_obj_add_event_cb(tile, tile_click_cb, LV_EVENT_CLICKED, (void*)entry);
    }

    return scr;
}

} // namespace launcher

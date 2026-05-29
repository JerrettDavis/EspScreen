#include "settings.h"
#include "../../os/screen_router.h"
#include "../../ui/widgets.h"
#include "../../os/logger.h"
#include <lvgl.h>

namespace settings {

static void back_cb(lv_event_t* e) {
    screen_router::pop();
}

lv_obj_t* create_screen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t* info_lbl = lv_label_create(scr);
    lv_label_set_text(info_lbl, "Touch: factory cal (Phase 0)");
    lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(info_lbl, LV_ALIGN_TOP_MID, 0, 70);

    widgets::make_back_btn(scr, back_cb);
    return scr;
}

} // namespace settings

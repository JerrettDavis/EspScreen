#include "claude_stub.h"
#include "../../os/screen_router.h"
#include "../../ui/widgets.h"
#include <lvgl.h>

namespace claude_stub {

static void back_cb(lv_event_t* e) { screen_router::pop(); }

lv_obj_t* create_screen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);

    lv_obj_t* lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "Claude Widget\n(Phase 3)");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl);

    widgets::make_back_btn(scr, back_cb);
    return scr;
}

} // namespace claude_stub

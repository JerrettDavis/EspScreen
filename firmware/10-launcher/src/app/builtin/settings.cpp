#include "settings.h"
#include "../../os/screen_router.h"
#include "../../ui/widgets.h"
#include "../../hal/touch.h"
#include <lvgl.h>

namespace settings {

static void back_cb(lv_event_t* e) {
    screen_router::pop();
}

static void recal_cb(lv_event_t* e) {
    touch::run_calibration();
}

lv_obj_t* create_screen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t* btn_cal = lv_button_create(scr);
    lv_obj_set_size(btn_cal, 200, 50);
    lv_obj_align(btn_cal, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_add_event_cb(btn_cal, recal_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_cal = lv_label_create(btn_cal);
    lv_label_set_text(lbl_cal, "Recalibrate Touch");
    lv_obj_center(lbl_cal);

    widgets::make_back_btn(scr, back_cb);
    return scr;
}

} // namespace settings

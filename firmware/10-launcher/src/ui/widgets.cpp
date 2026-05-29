#include "widgets.h"
#include <lvgl.h>

namespace widgets {

lv_obj_t* make_tile(lv_obj_t* parent, const char* label, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 130, 100);
    lv_obj_set_style_radius(btn, 12, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_center(lbl);

    /* Note: caller is responsible for attaching the click callback with user_data */
    return btn;
}

lv_obj_t* make_back_btn(lv_obj_t* parent, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 70, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(lbl);

    return btn;
}

} // namespace widgets

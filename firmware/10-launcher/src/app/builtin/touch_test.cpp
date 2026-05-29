#include "touch_test.h"
#include "../../os/screen_router.h"
#include "../../ui/widgets.h"
#include <cstdio>
#include <lvgl.h>

namespace touch_test {

static lv_obj_t* s_dot   = nullptr;
static lv_obj_t* s_coord = nullptr;

/* LVGL indev event: update dot position and coord label */
static void indev_read_cb(lv_event_t* e) {
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    if (s_dot) {
        lv_obj_set_pos(s_dot, pt.x - 8, pt.y - 8);
    }
    if (s_coord) {
        char buf[32];
        snprintf(buf, sizeof(buf), "X:%d  Y:%d", (int)pt.x, (int)pt.y);
        lv_label_set_text(s_coord, buf);
    }
}

static void back_cb(lv_event_t* e) {
    screen_router::pop();
}

lv_obj_t* create_screen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    /* Coord label */
    s_coord = lv_label_create(scr);
    lv_label_set_text(s_coord, "Touch the screen");
    lv_obj_set_style_text_color(s_coord, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_coord, LV_ALIGN_TOP_MID, 0, 12);

    /* Moving dot */
    s_dot = lv_obj_create(scr);
    lv_obj_set_size(s_dot, 16, 16);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(0xff4444), 0);
    lv_obj_set_pos(s_dot, 0, 0);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);

    /* Capture press events on the full screen */
    lv_obj_add_event_cb(scr, indev_read_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, [](lv_event_t* e){
        if (s_dot) lv_obj_remove_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
        if (s_coord) lv_label_set_text(s_coord, "Tracking...");
    }, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, [](lv_event_t* e){
        if (s_dot) lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_RELEASED, NULL);

    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);

    widgets::make_back_btn(scr, back_cb);
    return scr;
}

} // namespace touch_test

#include "touch_test.h"
#include "../../os/screen_router.h"
#include "../../ui/widgets.h"
#include "../../ui/theme.h"
#include "../../ui/tokens.h"
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

static void screen_unloaded_cb(lv_event_t* e) {
    /* Null statics BEFORE delete so any in-flight indev/press lambda no-ops. */
    s_dot   = nullptr;
    s_coord = nullptr;
    screen_router::delete_on_unload(e);
}

lv_obj_t* create_screen() {
    lv_obj_t* scr = widgets::make_screen();
    widgets::make_topbar(scr, "Touch Test", back_cb);
    lv_obj_add_event_cb(scr, screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);

    /* Coord label */
    s_coord = lv_label_create(scr);
    lv_label_set_text(s_coord, "Touch the screen");
    lv_obj_add_style(s_coord, ui_theme::style_text_value(), 0);
    lv_obj_align(s_coord, LV_ALIGN_TOP_MID, 0, tok::TOPBAR_H + tok::SP_L);

    /* Moving dot */
    s_dot = lv_obj_create(scr);
    lv_obj_set_size(s_dot, 16, 16);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(tok::ACCENT), 0);
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

    return scr;
}

} // namespace touch_test

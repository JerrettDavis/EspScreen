#include "statusbar.h"
#include <Arduino.h>
#include <cstdio>
#include <lvgl.h>

/* Forward declarations from OS layer (implemented in 1e) */
// #include "../os/wifi_mgr.h"
// #include "../os/time_sync.h"

namespace statusbar {

static lv_obj_t* s_lbl_time  = nullptr;
static lv_obj_t* s_lbl_wifi  = nullptr;
static lv_obj_t* s_lbl_heap  = nullptr;

lv_obj_t* create(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_HOR_RES, 28);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0d0d1a), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 2, 0);

    s_lbl_time = lv_label_create(bar);
    lv_label_set_text(s_lbl_time, "--:--");
    lv_obj_align(s_lbl_time, LV_ALIGN_LEFT_MID, 4, 0);

    s_lbl_wifi = lv_label_create(bar);
    lv_label_set_text(s_lbl_wifi, "WiFi");   // ASCII (glyph-safe); statusbar is currently unused
    lv_obj_align(s_lbl_wifi, LV_ALIGN_CENTER, 0, 0);

    s_lbl_heap = lv_label_create(bar);
    lv_label_set_text(s_lbl_heap, "---KB");
    lv_obj_align(s_lbl_heap, LV_ALIGN_RIGHT_MID, -4, 0);

    return bar;
}

void update() {
    if (s_lbl_heap) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%luKB", (unsigned long)(esp_get_free_heap_size() / 1024));
        lv_label_set_text(s_lbl_heap, buf);
    }
    /* time_sync and wifi_mgr updates wired up in Stage 1e */
}

} // namespace statusbar

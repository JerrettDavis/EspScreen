#include "about.h"
#include "../../os/screen_router.h"
#include "../../ui/widgets.h"
#include <Arduino.h>
#include <lvgl.h>

namespace about {

static void back_cb(lv_event_t* e) {
    screen_router::pop();
}

lv_obj_t* create_screen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "About EspScreen");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    /* Version + heap info */
    char info[128];
    snprintf(info, sizeof(info),
        "Version: %s\nPhase: %d\nFree heap: %lu B",
        ESPSCREEN_VERSION,
        ESPSCREEN_PHASE,
        (unsigned long)esp_get_free_heap_size()
    );

    lv_obj_t* lbl = lv_label_create(scr);
    lv_label_set_text(lbl, info);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 60);

    widgets::make_back_btn(scr, back_cb);
    return scr;
}

} // namespace about

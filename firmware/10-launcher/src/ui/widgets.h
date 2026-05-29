#pragma once
#include <lvgl.h>

namespace widgets {
    /**
     * Create a rounded app tile button.
     * @param parent  Parent container
     * @param label   Text shown on the tile
     * @param cb      Click callback
     */
    lv_obj_t* make_tile(lv_obj_t* parent, const char* label, lv_event_cb_t cb);

    /** Create a full-screen back button (bottom-right corner). */
    lv_obj_t* make_back_btn(lv_obj_t* parent, lv_event_cb_t cb);
}

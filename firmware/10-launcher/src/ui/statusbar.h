#pragma once
#include <lvgl.h>

namespace statusbar {
    /** Create the status bar at the top of the screen. Height = 28 px. */
    lv_obj_t* create(lv_obj_t* parent);

    /** Refresh WiFi/time/uptime labels. Call from loop or an LVGL timer. */
    void update();
}

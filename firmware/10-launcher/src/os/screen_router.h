#pragma once
#include <lvgl.h>

namespace screen_router {
    /**
     * Navigate to a screen, pushing current onto the back stack.
     * Uses lv_scr_load_anim with FADE_ON.
     */
    void push(lv_obj_t* screen);

    /** Pop back to previous screen. No-op if at root. */
    void pop();

    /** Navigate to root (launcher) without animation. */
    void go_home();
}

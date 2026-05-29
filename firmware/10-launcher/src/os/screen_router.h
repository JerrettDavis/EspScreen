#pragma once
#include <lvgl.h>

namespace screen_router {
    /**
     * Navigate to a screen, pushing current onto the back stack.
     * Uses lv_scr_load_anim with FADE_IN.
     */
    void push(lv_obj_t* screen);

    /**
     * Push a screen onto the back stack WITHOUT triggering lv_scr_load_anim.
     * Use when the caller will handle the actual screen load (e.g. lv_scr_load).
     * This avoids animation conflicts when the caller needs an immediate switch.
     */
    void push_silent(lv_obj_t* screen);

    /** Pop back to previous screen. No-op if at root. */
    void pop();

    /** Navigate to root (launcher) without animation. */
    void go_home();
}

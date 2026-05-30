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

    /**
     * Attach to a screen via LV_EVENT_SCREEN_UNLOADED. Deletes the screen
     * (async) once its fade-out animation has fully completed. Crash-safe:
     * never deletes the currently-active screen. Use this on EVERY pushed
     * screen so the router's pop()/go_home() reclaim heap.
     */
    void delete_on_unload(lv_event_t* e);

    /** Convenience: register delete_on_unload on `scr`. Call in each create_screen(). */
    void attach_autodelete(lv_obj_t* scr);
}

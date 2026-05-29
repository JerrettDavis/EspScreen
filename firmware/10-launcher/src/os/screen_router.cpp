#include "screen_router.h"
#include <lvgl.h>
#include <Arduino.h>

/* Simple stack — max 8 deep */
#define STACK_MAX 8
static lv_obj_t* s_stack[STACK_MAX];
static int s_top = -1;

namespace screen_router {

void push(lv_obj_t* screen) {
    lv_obj_t* active_before = lv_scr_act();
    if (s_top < STACK_MAX - 1) {
        s_stack[++s_top] = screen;
    }
    Serial.printf("[router] push: scr=%p (active_before=%p stack_depth=%d)\n",
                  (void*)screen, (void*)active_before, s_top);
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
    Serial.printf("[router] push: lv_scr_load_anim queued (active_now=%p)\n",
                  (void*)lv_scr_act());
}

void push_silent(lv_obj_t* screen) {
    lv_obj_t* active_before = lv_scr_act();
    if (s_top < STACK_MAX - 1) {
        s_stack[++s_top] = screen;
    }
    Serial.printf("[router] push_silent: scr=%p (active_before=%p stack_depth=%d) — no anim\n",
                  (void*)screen, (void*)active_before, s_top);
    /* Caller is responsible for calling lv_scr_load(screen) */
}

void pop() {
    if (s_top > 0) {
        lv_obj_t* prev = s_stack[--s_top];
        Serial.printf("[router] pop: to scr=%p (stack_depth=%d active_now=%p)\n",
                      (void*)prev, s_top, (void*)lv_scr_act());
        lv_scr_load_anim(prev, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
    } else {
        Serial.printf("[router] pop: already at root (stack_depth=%d) — no-op\n", s_top);
    }
}

void go_home() {
    if (s_top > 0) {
        lv_obj_t* root = s_stack[0];
        s_top = 0;
        Serial.printf("[router] go_home: to scr=%p\n", (void*)root);
        lv_scr_load_anim(root, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    }
}

} // namespace screen_router

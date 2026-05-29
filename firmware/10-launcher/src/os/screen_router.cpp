#include "screen_router.h"
#include <lvgl.h>

/* Simple stack — max 8 deep */
#define STACK_MAX 8
static lv_obj_t* s_stack[STACK_MAX];
static int s_top = -1;

namespace screen_router {

void push(lv_obj_t* screen) {
    if (s_top < STACK_MAX - 1) {
        s_stack[++s_top] = screen;
    }
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void pop() {
    if (s_top > 0) {
        lv_obj_t* prev = s_stack[--s_top];
        lv_scr_load_anim(prev, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
    }
}

void go_home() {
    if (s_top > 0) {
        lv_obj_t* root = s_stack[0];
        s_top = 0;
        lv_scr_load_anim(root, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    }
}

} // namespace screen_router

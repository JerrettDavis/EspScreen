/**
 * sim_registry.h — table of screens the simulator can render.
 *
 * Phase 1 contains a single entry: the launcher screen. Later phases append
 * the other builtin screens here so the headless harness / interactive sim /
 * validator can iterate over every screen by id.
 */
#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* id;              /* stable key, e.g. "launcher" */
    lv_obj_t* (*create)(void);   /* factory returning a freshly built screen */
} sim_screen_t;

extern const sim_screen_t sim_registry[];
extern const int          sim_registry_count;

#ifdef __cplusplus
}
#endif

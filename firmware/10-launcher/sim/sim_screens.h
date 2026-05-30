/**
 * sim_screens.h — sim-only entry points into otherwise file-static screen
 * factories, so the validator / full-screen harness (P2) can render sub-screens
 * that are not reachable from the public create_screen() API.
 *
 * Guarded so it has ZERO effect on the device build (esp32dev does not define
 * ESPSCREEN_SIM or SCREEN_VALIDATE).
 */
#pragma once

#if defined(ESPSCREEN_SIM) || defined(SCREEN_VALIDATE)
#include <lvgl.h>

namespace settings {
    /** Build the (normally file-static) Claude Profiles sub-screen. */
    lv_obj_t* sim_claude_profiles_screen();
    /** Build the (normally file-static) WiFi Networks sub-screen. */
    lv_obj_t* sim_wifi_networks_screen();
}
#endif

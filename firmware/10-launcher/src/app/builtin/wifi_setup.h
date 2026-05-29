#pragma once
#include <lvgl.h>

/**
 * wifi_setup — On-screen WiFi scan + credential entry flow.
 *
 * Single pushed screen whose content is swapped across three steps:
 *   Step 1 — Scan list   (scrollable flex-column of lv_button rows)
 *   Step 2 — Password    (lv_textarea + lv_keyboard)
 *   Step 3 — Connect     (status label; pops on success after 1.5 s)
 *
 * Memory strategy (48 KB LVGL heap, no PSRAM):
 *   The scan container (Step 1) is deleted before the keyboard is created
 *   (Step 2) so the two heavyweight allocations never coexist.
 *   Heap is logged immediately after keyboard creation.
 *
 * Lifecycle:
 *   LV_EVENT_DELETE is registered on the screen; all file-static widget
 *   pointers are nulled and one-shot timers are deleted in that handler,
 *   mirroring the claude_widget.cpp pattern.
 */

namespace wifi_setup {

/**
 * Allocate the WiFi-setup screen and return it.
 * The caller is responsible for pushing it:
 *   screen_router::push(wifi_setup::create_screen());
 *
 * The screen manages its own lifecycle via LV_EVENT_DELETE.
 */
lv_obj_t* create_screen();

} // namespace wifi_setup

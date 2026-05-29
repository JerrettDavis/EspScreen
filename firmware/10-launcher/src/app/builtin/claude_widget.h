#pragma once
#include <lvgl.h>

/**
 * claude_widget — Live Claude usage dashboard.
 *
 * Polls GET <endpoint>/status.json every 60s.
 * Endpoint URL stored in NVS key "claude"/"endpoint".
 * WiFi credentials managed by wifi_mgr.
 *
 * Screen layout (320x480 portrait):
 *   - Top bar: back btn, "Claude Usage" title, optional stale badge
 *   - Connection dot (green/red) + model name
 *   - 5-hour utilization bar + reset countdown
 *   - 7-day utilization bar + reset countdown
 *   - Session section: cost, context %, cache TTL
 *   - Headroom section (hidden when disabled): tokens saved, compression %
 *   - Bottom: timestamp + manual refresh button
 */
namespace claude_widget {
    /** Build and return the widget screen. Register as app factory in app_registry.cpp. */
    lv_obj_t* create_screen();

    /** Force an immediate HTTP poll (also called by the refresh button). */
    void poll_now();

    /** Cleanup — called by screen_router when this screen is popped off the stack. */
    void delete_screen();
}

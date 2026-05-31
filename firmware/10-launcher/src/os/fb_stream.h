/**
 * fb_stream.h — live device framebuffer streamer (debug-only, gated -DFB_STREAM).
 *
 * Taps the LVGL flush_cb in display.cpp and streams each rendered tile over a
 * raw TCP socket (port 8090) to the desktop viewer. Captures exactly what LVGL
 * drew on-device, BEFORE TFT_eSPI's hardware byte-swap / SPI push — the
 * reference frame for the 3-way tofu diagnosis.
 *
 * Discipline: on_flush() NEVER blocks the LVGL loop. If the socket can't take a
 * whole tile right now, the tile is DROPPED (the next flush supersedes it).
 * Zero framebuffer allocation — tiles stream straight through.
 *
 * Compiled only into [env:esp32dev_debug]; absent from production esp32dev.
 */
#pragma once
#include <lvgl.h>

namespace fb_stream {
    /** Start the TCP server (call once in setup() after WiFi/net is up). */
    void init();

    /** Accept a pending client / housekeeping. Call from the main loop. */
    void loop();

    /** Stream one flush tile, non-blocking. Call from flush_cb. */
    void on_flush(const lv_area_t* area, const uint8_t* px_map);

    /** Emit a frame-end sentinel (w==h==0) so the viewer can finalize a frame. */
    void frame_end();
}

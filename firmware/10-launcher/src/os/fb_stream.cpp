/**
 * fb_stream.cpp — live device framebuffer streamer (debug-only, -DFB_STREAM).
 *
 * Raw TCP server on :8090, single client. Streams each LVGL flush tile as a
 * 10-byte fb_tile_hdr_t + w*h*2 RGB565-LE bytes (see sim/fb_proto.h).
 *
 * Non-blocking discipline: a tile is sent only via bounded best-effort writes
 * that respect WiFiClient::availableForWrite(); if the socket can't drain within
 * a small spin cap, the tile is ABANDONED. Every tile begins with magic+seq so
 * the viewer resynchronizes on the next header after any abandoned/partial tile.
 * No framebuffer is allocated — tiles stream straight through px_map.
 *
 * On client connect we invalidate the whole active screen so a freshly connected
 * viewer receives a complete frame even for a static screen.
 */
#ifdef FB_STREAM   /* entire module is debug-only; compiles to nothing otherwise */

#include "fb_stream.h"
#include "../../sim/fb_proto.h"
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

namespace fb_stream {

static WiFiServer s_server(FB_PROTO_PORT);
static WiFiClient s_client;
static bool       s_started        = false;
static uint8_t    s_seq            = 0;
static bool       s_need_full_frame = false;

/* Bounded best-effort write: push `len` bytes in chunks sized to the socket's
 * current write headroom, yielding while LWIP drains. A whole 320x20 tile is
 * ~12.8 KB — far larger than the ESP32 default TCP send buffer (~5.7 KB) — so a
 * single write() of the full tile can't land; we must chunk by availableForWrite
 * and wait for ACKs between chunks. Bounded by a wall-clock budget (not a tiny
 * spin count) so flush_cb can't stall the LVGL loop indefinitely; if the budget
 * is exceeded the tile is ABANDONED (the next tile's magic lets the viewer
 * resync). This is a DEBUG build, so a short bounded wait here is acceptable. */
static bool write_all_bounded(const uint8_t* data, size_t len) {
    size_t off = 0;
    const uint32_t deadline = millis() + 80;   /* per-call budget (ms) */
    while (off < len) {
        if ((int32_t)(millis() - deadline) > 0) return false;   /* give up */
        int avail = s_client.availableForWrite();
        if (avail <= 0) { delay(1); continue; }   /* wait for the window to open */
        size_t chunk = (size_t)avail;
        if (chunk > len - off) chunk = len - off;
        size_t wrote = s_client.write(data + off, chunk);
        if (wrote == 0) { delay(1); continue; }
        off += wrote;
    }
    return true;
}

void init() {
    if (s_started) return;
    s_server.begin();
    s_server.setNoDelay(true);
    s_started = true;
    Serial.printf("[fb_stream] listening on :%d\n", FB_PROTO_PORT);
}

void loop() {
    if (!s_started) return;

    /* Accept a new client (single-client model: replace any old one). */
    if (s_server.hasClient()) {
        if (s_client && s_client.connected()) s_client.stop();
        s_client = s_server.available();
        s_client.setNoDelay(true);
        s_need_full_frame = true;
        Serial.println("[fb_stream] client connected");
    }

    /* On (re)connect, force a full redraw so the viewer gets a whole frame. */
    if (s_need_full_frame && s_client && s_client.connected()) {
        s_need_full_frame = false;
        lv_obj_t* scr = lv_scr_act();
        if (scr) {
            lv_obj_invalidate(scr);
            lv_refr_now(NULL);     /* triggers flush_cb tiles for the whole screen */
            frame_end();
        }
    }
}

void on_flush(const lv_area_t* area, const uint8_t* px_map) {
    if (!s_client || !s_client.connected()) return;

    const uint16_t w = (uint16_t)(area->x2 - area->x1 + 1);
    const uint16_t h = (uint16_t)(area->y2 - area->y1 + 1);

    fb_tile_hdr_t hdr;
    hdr.magic = FB_PROTO_MAGIC;
    hdr.seq   = s_seq++;
    hdr.x     = (uint16_t)area->x1;
    hdr.y     = (uint16_t)area->y1;
    hdr.w     = w;
    hdr.h     = h;

    const size_t payload = (size_t)w * h * 2u;

    /* Header then payload. If either can't complete within the bounded budget,
     * the tile is abandoned; the next tile's magic lets the viewer resync. */
    if (!write_all_bounded((const uint8_t*)&hdr, sizeof(hdr))) return;
    write_all_bounded(px_map, payload);
}

void frame_end() {
    if (!s_client || !s_client.connected()) return;
    fb_tile_hdr_t hdr;
    hdr.magic = FB_PROTO_MAGIC;
    hdr.seq   = s_seq++;
    hdr.x = hdr.y = 0;
    hdr.w = hdr.h = 0;      /* sentinel */
    write_all_bounded((const uint8_t*)&hdr, sizeof(hdr));
}

} // namespace fb_stream

#endif // FB_STREAM

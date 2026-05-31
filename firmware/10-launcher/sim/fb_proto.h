/**
 * fb_proto.h — framebuffer-tile wire protocol, SHARED by the device streamer
 * (src/os/fb_stream.cpp) and the desktop viewer (sim/sim_main.cpp).
 *
 * Each LVGL flush region (a "tile") is sent as a fixed 10-byte header followed
 * immediately by exactly w*h*2 bytes of RGB565 little-endian pixel data — the
 * raw LVGL px_map tapped at flush_cb, BEFORE TFT_eSPI's hardware byte-swap and
 * SPI push. (display.cpp uses pushColors(..., true) to swap at push time, so
 * the bytes here are standard RGB565-LE, identical to what the host renders.)
 *
 * A frame-end sentinel tile (w == 0 && h == 0) marks "the visible frame is now
 * complete" so the viewer knows when a full 320x480 image has been assembled.
 */
#pragma once
#include <stdint.h>

#define FB_PROTO_MAGIC 0xFB
#define FB_PROTO_PORT  8090

#pragma pack(push, 1)
typedef struct {
    uint8_t  magic;   /* 0xFB */
    uint8_t  seq;     /* incrementing tile counter (wraps) */
    uint16_t x, y;    /* tile top-left in LVGL coords */
    uint16_t w, h;    /* tile size; w==0 && h==0 → frame-end sentinel */
    /* followed by w*h*2 bytes RGB565-LE (none for the sentinel) */
} fb_tile_hdr_t;
#pragma pack(pop)

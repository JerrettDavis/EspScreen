/**
 * screen_mirror.cpp — Lightweight screen capture + BMP-over-HTTP for ESP32.
 *
 * Maintains a 80×120 shadow buffer in .bss (no heap) that is populated by
 * stride-decimating every LVGL flush.  The shadow is always in native
 * little-endian RGB565 (same byte order as px_map from TFT_eSPI / LVGL).
 *
 * write_bmp() streams an uncompressed 16-bpp BI_BITFIELDS BMP directly to the
 * WebServer response using a per-row stack buffer — no full-image heap alloc.
 *
 * Thread safety: all state is touched only from the Arduino loop task (LVGL
 * flush + WebServer handle both run there), so no mutex is needed.
 */

#include "screen_mirror.h"
#include "logger.h"
#include <WebServer.h>
#include <Arduino.h>

namespace screen_mirror {

/* ── Shadow buffer ───────────────────────────────────────────────────────────
 * 80*120*2 = 19 200 bytes — heap-allocated by init() to avoid overflowing
 * the ESP32 DRAM0 .bss segment.  Pointer is null until init() is called;
 * all functions guard against a null pointer so pre-init calls are safe.
 */
static uint16_t* s_shadow  = nullptr;
static bool      s_enabled = false;

/* ── init ────────────────────────────────────────────────────────────────── */
void init() {
    if (s_shadow) return;   // idempotent
    s_shadow = (uint16_t*)malloc(CAP_W * CAP_H * sizeof(uint16_t));
    if (!s_shadow) {
        LOG_E("mirror", "init: malloc(%d) failed", CAP_W * CAP_H * 2);
        return;
    }
    memset(s_shadow, 0, CAP_W * CAP_H * sizeof(uint16_t));
    LOG_I("mirror", "init: shadow buffer allocated (%d bytes)", CAP_W * CAP_H * 2);
}

/* ── enable / enabled ────────────────────────────────────────────────────── */

void enable(bool on) {
    if (on && !s_shadow) {
        /* Auto-init if caller forgot (belt-and-suspenders) */
        init();
        if (!s_shadow) {
            LOG_E("mirror", "enable: cannot allocate shadow buffer — staying disabled");
            s_enabled = false;
            return;
        }
    }
    s_enabled = on && (s_shadow != nullptr);  /* Only enable if buffer is valid */
    if (s_enabled) {
        /* Force a full redraw so the shadow populates before the first request */
        lv_obj_invalidate(lv_screen_active());
        LOG_I("mirror", "enabled — full redraw requested");
    } else if (on && !s_shadow) {
        /* Caller requested enable but no buffer available */
        LOG_W("mirror", "enable: skipped — no shadow buffer");
    } else {
        LOG_I("mirror", "disabled");
    }
}

bool enabled() {
    return s_enabled;
}

/* ── dims ────────────────────────────────────────────────────────────────── */

void dims(int& w, int& h) {
    w = CAP_W;
    h = CAP_H;
}

/* ── on_flush ────────────────────────────────────────────────────────────── */
/* Integer 4×/4× stride decimation: one shadow pixel per 4×4 display pixels.
 * 320/80=4, 480/120=4.
 * Called from display::flush_cb() with every partial-render rect.
 */
void on_flush(const lv_area_t* area, const uint8_t* px_map) {
    if (!s_shadow) return;
    const int SX = 4, SY = 4;
    int sx0 = (area->x1 + SX - 1) / SX,  sx1 = area->x2 / SX;
    int sy0 = (area->y1 + SY - 1) / SY,  sy1 = area->y2 / SY;
    int rw  = area->x2 - area->x1 + 1;
    const uint16_t* src = (const uint16_t*)px_map;
    for (int sy = sy0; sy <= sy1 && sy < CAP_H; ++sy) {
        if (sy < 0) continue;
        int src_y = sy * SY - area->y1;
        for (int sx = sx0; sx <= sx1 && sx < CAP_W; ++sx) {
            if (sx < 0) continue;
            int src_x = sx * SX - area->x1;
            s_shadow[sy * CAP_W + sx] = src[src_y * rw + src_x];
        }
    }
}

/* ── write_bmp ───────────────────────────────────────────────────────────── */
/*
 * BMP layout (all integers little-endian):
 *
 *   Offset  Size  Field
 *   ──────  ────  ──────────────────────────────────────────────
 *        0    14  BITMAPFILEHEADER  ('BM', fileSize, 0,0, offset=66)
 *       14    40  BITMAPINFOHEADER  (size=40, w, -h, planes=1, bpp=16,
 *                                   compression=3 BI_BITFIELDS, imageSize,
 *                                   0,0,0,0)
 *       54    12  Color masks       R=0xF800, G=0x07E0, B=0x001F
 *       66     …  Pixel data        top-down rows, each padded to 4 bytes
 *
 * Total header: 14+40+12 = 66 bytes.
 */
bool write_bmp(WebServer& server, int out_w, int out_h) {
    if (!s_enabled || !s_shadow) return false;

    /* Clamp dimensions */
    if (out_w < 16)    out_w = 16;
    if (out_w > CAP_W) out_w = CAP_W;
    if (out_h < 24)    out_h = 24;
    if (out_h > CAP_H) out_h = CAP_H;

    /* Row stride padded to 4-byte boundary */
    int rowbytes  = (out_w * 2 + 3) & ~3;
    int imageSize = rowbytes * out_h;
    int fileSize  = 66 + imageSize;

    /* ── Build 66-byte header on the stack ─────────────────────────────── */
    uint8_t hdr[66];
    memset(hdr, 0, sizeof(hdr));

    /* BITMAPFILEHEADER (14 bytes) */
    hdr[0]  = 'B'; hdr[1] = 'M';
    hdr[2]  = (uint8_t)(fileSize);
    hdr[3]  = (uint8_t)(fileSize >> 8);
    hdr[4]  = (uint8_t)(fileSize >> 16);
    hdr[5]  = (uint8_t)(fileSize >> 24);
    /* reserved: bytes 6-9 already 0 */
    hdr[10] = 66;   /* pixel data offset, LE u32 — fits in one byte */
    /* hdr[11..13] = 0 */

    /* BITMAPINFOHEADER (40 bytes) at offset 14 */
    /* size = 40 */
    hdr[14] = 40;
    /* width */
    hdr[18] = (uint8_t)(out_w);
    hdr[19] = (uint8_t)(out_w >> 8);
    hdr[20] = (uint8_t)(out_w >> 16);
    hdr[21] = (uint8_t)(out_w >> 24);
    /* height — negative for top-down */
    int neg_h = -out_h;
    hdr[22] = (uint8_t)(neg_h);
    hdr[23] = (uint8_t)(neg_h >> 8);
    hdr[24] = (uint8_t)(neg_h >> 16);
    hdr[25] = (uint8_t)(neg_h >> 24);
    /* planes = 1 */
    hdr[26] = 1;
    /* bpp = 16 */
    hdr[28] = 16;
    /* compression = 3 (BI_BITFIELDS) */
    hdr[30] = 3;
    /* imageSize */
    hdr[34] = (uint8_t)(imageSize);
    hdr[35] = (uint8_t)(imageSize >> 8);
    hdr[36] = (uint8_t)(imageSize >> 16);
    hdr[37] = (uint8_t)(imageSize >> 24);
    /* pixels-per-meter X/Y, clrUsed, clrImportant — all 0 */

    /* RGB565 color masks (12 bytes) at offset 54 */
    /* R mask = 0x0000F800 */
    hdr[54] = 0x00; hdr[55] = 0xF8; hdr[56] = 0x00; hdr[57] = 0x00;
    /* G mask = 0x000007E0 */
    hdr[58] = 0xE0; hdr[59] = 0x07; hdr[60] = 0x00; hdr[61] = 0x00;
    /* B mask = 0x0000001F */
    hdr[62] = 0x1F; hdr[63] = 0x00; hdr[64] = 0x00; hdr[65] = 0x00;

    /* ── Stream response ───────────────────────────────────────────────── */
    server.setContentLength(fileSize);
    server.send(200, "image/bmp", "");
    server.sendContent((const char*)hdr, 66);

    /* Per-row stack buffer: max 80*2 + 2 pad = 162 bytes */
    uint8_t rowbuf[CAP_W * 2 + 2];

    for (int oy = 0; oy < out_h; ++oy) {
        /* Clear padding bytes */
        memset(rowbuf, 0, rowbytes);

        int shadow_y = (oy * CAP_H) / out_h;
        for (int ox = 0; ox < out_w; ++ox) {
            int shadow_x = (ox * CAP_W) / out_w;
            uint16_t px  = s_shadow[shadow_y * CAP_W + shadow_x];
            /* LE u16: low byte first, then high byte */
            rowbuf[ox * 2]     = (uint8_t)(px);
            rowbuf[ox * 2 + 1] = (uint8_t)(px >> 8);
        }
        server.sendContent((const char*)rowbuf, rowbytes);
    }

    LOG_I("mirror", "write_bmp: %dx%d → %d bytes", out_w, out_h, fileSize);
    return true;
}

} // namespace screen_mirror

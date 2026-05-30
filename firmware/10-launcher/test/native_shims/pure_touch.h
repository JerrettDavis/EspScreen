/**
 * pure_touch.h — Portable shim for touch coordinate scaling.
 * Replicates the scaling logic from web_portal.cpp POST /api/touch.
 *
 * Scales (x, y) from rendered image space (w x h) into LVGL 320x480 space.
 * Clamps result to [0,319] x [0,479].
 * Handles w==0 and h==0 gracefully (no division by zero).
 */
#pragma once

/**
 * Scale touch coordinates from image space to LVGL screen space.
 *
 * @param x      Input X in image coordinates
 * @param y      Input Y in image coordinates
 * @param w      Image width (0 treated as 1 to avoid div-by-zero)
 * @param h      Image height (0 treated as 1 to avoid div-by-zero)
 * @param lv_x   Output: LVGL X in [0, 319]
 * @param lv_y   Output: LVGL Y in [0, 479]
 */
inline void scale_touch(int x, int y, int w, int h, int* lv_x, int* lv_y) {
    int eff_w = (w > 0) ? w : 1;
    int eff_h = (h > 0) ? h : 1;

    int rx = (int)((long)x * 320 / eff_w);
    int ry = (int)((long)y * 480 / eff_h);

    if (rx < 0)   rx = 0;
    if (rx > 319) rx = 319;
    if (ry < 0)   ry = 0;
    if (ry > 479) ry = 479;

    *lv_x = rx;
    *lv_y = ry;
}

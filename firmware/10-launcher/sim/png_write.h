/**
 * png_write.h — single-include PNG writer for the simulator.
 *
 * Wraps the vendored public-domain stb_image_write.h. Exactly ONE translation
 * unit must define SIM_PNG_WRITE_IMPL before including this header (the headless
 * main does), which pulls in the stb implementation. All other includers get
 * just the declaration of sim_write_png_rgb888().
 */
#pragma once

#include <cstdint>

#ifdef SIM_PNG_WRITE_IMPL
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
#include "stb_image_write.h"

/**
 * Write an RGB888 buffer (3 bytes/pixel, row-major, top-left origin) to a PNG.
 * @return non-zero on success (stb convention).
 */
static inline int sim_write_png_rgb888(const char* path, int w, int h,
                                       const uint8_t* rgb888) {
    return stbi_write_png(path, w, h, 3, rgb888, w * 3);
}

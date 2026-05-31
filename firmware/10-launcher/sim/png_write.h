/**
 * png_write.h — single-include PNG image I/O for the simulator (write + load).
 *
 * Wraps the vendored public-domain stb_image_write.h and stb_image.h. Exactly
 * ONE translation unit must define SIM_PNG_WRITE_IMPL before including this
 * header (the headless main does); that TU pulls in both stb implementations.
 * All other includers get just the inline wrapper declarations.
 */
#pragma once

#include <cstdint>
#include <cstdlib>

#ifdef SIM_PNG_WRITE_IMPL
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_image_write.h"
#include "stb_image.h"

/**
 * Write an RGB888 buffer (3 bytes/pixel, row-major, top-left origin) to a PNG.
 * @return non-zero on success (stb convention).
 */
static inline int sim_write_png_rgb888(const char* path, int w, int h,
                                       const uint8_t* rgb888) {
    return stbi_write_png(path, w, h, 3, rgb888, w * 3);
}

/**
 * Load a PNG as RGB888 (forces 3 channels). Caller must stbi_image_free() the
 * returned buffer. Writes the file's dimensions to *w / *h.
 * @return pointer to w*h*3 bytes, or NULL on failure.
 */
static inline uint8_t* sim_load_png_rgb888(const char* path, int* w, int* h) {
    int channels = 0;
    return stbi_load(path, w, h, &channels, 3);   /* force RGB */
}

static inline void sim_free_png(uint8_t* p) {
    stbi_image_free(p);
}

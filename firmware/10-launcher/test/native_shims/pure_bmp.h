/**
 * pure_bmp.h — Portable shim for BMP header generation.
 * Replicates the 66-byte header built by screen_mirror.cpp::write_bmp().
 *
 * BMP layout:
 *   Offset  Size  Content
 *    0       14   BITMAPFILEHEADER: 'BM', fileSize(LE u32), 0,0, offset=66
 *   14       40   BITMAPINFOHEADER: size=40, w(LE i32), -h(LE i32),
 *                  planes=1, bpp=16, compression=3(BI_BITFIELDS),
 *                  imageSize(LE u32), 0,0,0,0
 *   54       12   Color masks: R=0xF800, G=0x07E0, B=0x001F (all LE u32)
 *   66        –   Pixel data
 *
 * Row stride: rowbytes = (out_w*2 + 3) & ~3  (4-byte aligned)
 * Image size: rowbytes * out_h
 * File size:  66 + imageSize
 */
#pragma once
#include <stdint.h>
#include <string.h>

/** Row stride for a given output width (padded to 4-byte boundary). */
inline int bmp_rowbytes(int out_w) {
    return (out_w * 2 + 3) & ~3;
}

/** Total file size in bytes for a given output width and height. */
inline int bmp_filesize(int out_w, int out_h) {
    return 66 + bmp_rowbytes(out_w) * out_h;
}

/**
 * Fill a 66-byte BMP header buffer.
 * Exactly matches the header written by screen_mirror.cpp::write_bmp().
 */
inline void fill_bmp_header(uint8_t hdr[66], int out_w, int out_h) {
    memset(hdr, 0, 66);

    int rowbytes  = bmp_rowbytes(out_w);
    int imageSize = rowbytes * out_h;
    int fileSize  = 66 + imageSize;

    /* BITMAPFILEHEADER (14 bytes) */
    hdr[0]  = 'B'; hdr[1] = 'M';
    hdr[2]  = (uint8_t)(fileSize);
    hdr[3]  = (uint8_t)(fileSize >> 8);
    hdr[4]  = (uint8_t)(fileSize >> 16);
    hdr[5]  = (uint8_t)(fileSize >> 24);
    /* reserved: bytes 6-9 = 0 */
    hdr[10] = 66;   /* pixel data offset, LE u32 — fits in one byte */

    /* BITMAPINFOHEADER (40 bytes) at offset 14 */
    hdr[14] = 40;   /* size */
    /* width */
    hdr[18] = (uint8_t)(out_w);
    hdr[19] = (uint8_t)(out_w >> 8);
    hdr[20] = (uint8_t)(out_w >> 16);
    hdr[21] = (uint8_t)(out_w >> 24);
    /* height — negative for top-down */
    int neg_h = -out_h;
    hdr[22] = (uint8_t)((unsigned int)neg_h);
    hdr[23] = (uint8_t)((unsigned int)neg_h >> 8);
    hdr[24] = (uint8_t)((unsigned int)neg_h >> 16);
    hdr[25] = (uint8_t)((unsigned int)neg_h >> 24);
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

    /* RGB565 color masks (12 bytes) at offset 54 */
    /* R mask = 0x0000F800 — LE: 00 F8 00 00 */
    hdr[54] = 0x00; hdr[55] = 0xF8; hdr[56] = 0x00; hdr[57] = 0x00;
    /* G mask = 0x000007E0 — LE: E0 07 00 00 */
    hdr[58] = 0xE0; hdr[59] = 0x07; hdr[60] = 0x00; hdr[61] = 0x00;
    /* B mask = 0x0000001F — LE: 1F 00 00 00 */
    hdr[62] = 0x1F; hdr[63] = 0x00; hdr[64] = 0x00; hdr[65] = 0x00;
}

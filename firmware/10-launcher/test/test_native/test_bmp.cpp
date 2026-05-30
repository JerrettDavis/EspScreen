/**
 * test_bmp.cpp — Unity tests for pure_bmp.h (BMP header generation).
 * Part of the native test suite under test/native/.
 * Runner is invoked from test_claude.cpp's main().
 */
#include "../native_shims/pure_bmp.h"
#include <unity.h>
#include <stdint.h>

static void test_rowbytes_aligned(void) {
    /* 80 pixels * 2 bytes = 160, already 4-byte aligned */
    TEST_ASSERT_EQUAL_INT(160, bmp_rowbytes(80));
}

static void test_rowbytes_padded(void) {
    /* 3 pixels * 2 bytes = 6, padded to 8 */
    TEST_ASSERT_EQUAL_INT(8, bmp_rowbytes(3));
}

static void test_filesize(void) {
    /* 80x120: rowbytes=160, imageSize=160*120=19200, fileSize=66+19200=19266 */
    TEST_ASSERT_EQUAL_INT(19266, bmp_filesize(80, 120));
}

static void test_header_signature(void) {
    uint8_t hdr[66];
    fill_bmp_header(hdr, 80, 120);
    TEST_ASSERT_EQUAL_INT('B', hdr[0]);
    TEST_ASSERT_EQUAL_INT('M', hdr[1]);
}

static void test_header_bpp(void) {
    uint8_t hdr[66];
    fill_bmp_header(hdr, 80, 120);
    /* bpp field at offset 28 */
    TEST_ASSERT_EQUAL_INT(16, hdr[28]);
}

static void test_header_compression(void) {
    uint8_t hdr[66];
    fill_bmp_header(hdr, 80, 120);
    /* compression = 3 (BI_BITFIELDS) at offset 30 */
    TEST_ASSERT_EQUAL_INT(3, hdr[30]);
}

static void test_header_height_negative(void) {
    uint8_t hdr[66];
    fill_bmp_header(hdr, 80, 120);
    /* height is a signed LE int32 at offset 22; -120 = 0xFFFFFF88 in two's complement */
    int32_t h = (int32_t)(
        (uint32_t)hdr[22]         |
        ((uint32_t)hdr[23] << 8)  |
        ((uint32_t)hdr[24] << 16) |
        ((uint32_t)hdr[25] << 24)
    );
    TEST_ASSERT_EQUAL_INT32(-120, h);
}

static void test_header_filesize_le(void) {
    uint8_t hdr[66];
    fill_bmp_header(hdr, 80, 120);
    /* fileSize LE u32 at offset 2 */
    uint32_t fs = (uint32_t)hdr[2]         |
                  ((uint32_t)hdr[3] << 8)  |
                  ((uint32_t)hdr[4] << 16) |
                  ((uint32_t)hdr[5] << 24);
    TEST_ASSERT_EQUAL_UINT32(19266u, fs);
}

void run_test_bmp(void) {
    RUN_TEST(test_rowbytes_aligned);
    RUN_TEST(test_rowbytes_padded);
    RUN_TEST(test_filesize);
    RUN_TEST(test_header_signature);
    RUN_TEST(test_header_bpp);
    RUN_TEST(test_header_compression);
    RUN_TEST(test_header_height_negative);
    RUN_TEST(test_header_filesize_le);
}

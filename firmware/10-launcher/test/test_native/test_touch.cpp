/**
 * test_touch.cpp — Unity tests for pure_touch.h (touch coordinate scaling).
 * Part of the native test suite under test/native/.
 * Runner is invoked from test_claude.cpp's main().
 */
#include "../native_shims/pure_touch.h"
#include <unity.h>

static void test_scale_center(void) {
    int lv_x, lv_y;
    /* Image 320x480, click at exact center → maps to same coords in LVGL space */
    scale_touch(160, 240, 320, 480, &lv_x, &lv_y);
    TEST_ASSERT_EQUAL_INT(160, lv_x);
    TEST_ASSERT_EQUAL_INT(240, lv_y);
}

static void test_scale_clamp_max(void) {
    int lv_x, lv_y;
    /* Coordinates beyond image dimensions → clamped to max */
    scale_touch(400, 600, 320, 480, &lv_x, &lv_y);
    TEST_ASSERT_EQUAL_INT(319, lv_x);
    TEST_ASSERT_EQUAL_INT(479, lv_y);
}

static void test_scale_zero_size_no_divzero(void) {
    int lv_x, lv_y;
    /* w=0, h=0: must not divide by zero; result clamps to (0,0) */
    scale_touch(0, 0, 0, 0, &lv_x, &lv_y);
    TEST_ASSERT_EQUAL_INT(0, lv_x);
    TEST_ASSERT_EQUAL_INT(0, lv_y);
}

static void test_scale_origin(void) {
    int lv_x, lv_y;
    scale_touch(0, 0, 320, 480, &lv_x, &lv_y);
    TEST_ASSERT_EQUAL_INT(0, lv_x);
    TEST_ASSERT_EQUAL_INT(0, lv_y);
}

static void test_scale_quarter_image(void) {
    int lv_x, lv_y;
    /* Image 80x120 (1/4 of LVGL), click center → scales to LVGL center */
    scale_touch(40, 60, 80, 120, &lv_x, &lv_y);
    TEST_ASSERT_EQUAL_INT(160, lv_x);
    TEST_ASSERT_EQUAL_INT(240, lv_y);
}

void run_test_touch(void) {
    RUN_TEST(test_scale_center);
    RUN_TEST(test_scale_clamp_max);
    RUN_TEST(test_scale_zero_size_no_divzero);
    RUN_TEST(test_scale_origin);
    RUN_TEST(test_scale_quarter_image);
}

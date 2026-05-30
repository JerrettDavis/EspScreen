/**
 * test_config.cpp — Unity tests for pure_config.h (MirrorCfg clamping).
 * Part of the native test suite under test/native/.
 * Runner is invoked from test_claude.cpp's main().
 */
#include "../native_shims/pure_config.h"
#include <unity.h>

static void test_interval_too_low(void) {
    MirrorCfg m = { false, 50, 80, 120 };
    clamp_mirror(m);
    TEST_ASSERT_EQUAL_INT(100, m.interval_ms);
}

static void test_interval_too_high(void) {
    MirrorCfg m = { false, 5000, 80, 120 };
    clamp_mirror(m);
    TEST_ASSERT_EQUAL_INT(2000, m.interval_ms);
}

static void test_width_too_high(void) {
    MirrorCfg m = { false, 300, 200, 60 };
    clamp_mirror(m);
    TEST_ASSERT_EQUAL_INT(80, m.out_width);
}

static void test_height_too_low(void) {
    MirrorCfg m = { false, 300, 60, 10 };
    clamp_mirror(m);
    TEST_ASSERT_EQUAL_INT(24, m.out_height);
}

static void test_valid_values_unchanged(void) {
    MirrorCfg m = { true, 300, 60, 100 };
    clamp_mirror(m);
    TEST_ASSERT_EQUAL_INT(300, m.interval_ms);
    TEST_ASSERT_EQUAL_INT(60,  m.out_width);
    TEST_ASSERT_EQUAL_INT(100, m.out_height);
}

void run_test_config(void) {
    RUN_TEST(test_interval_too_low);
    RUN_TEST(test_interval_too_high);
    RUN_TEST(test_width_too_high);
    RUN_TEST(test_height_too_low);
    RUN_TEST(test_valid_values_unchanged);
}

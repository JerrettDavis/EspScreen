/**
 * test_claude.cpp — Unity tests for pure_claude.h (fmt_duration + parse_reset_secs).
 * Part of the native test suite under test/native/.
 */
#include "../native_shims/pure_claude.h"
#include <unity.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── fmt_duration ──────────────────────────────────────────────────────────── */

void test_fmt_duration_hours_minutes(void) {
    char buf[32];
    fmt_duration(7265, buf, sizeof(buf));
    /* 7265 / 3600 = 2h, (7265 % 3600) / 60 = 1m → "2h 1m" */
    TEST_ASSERT_EQUAL_STRING("2h 1m", buf);
}

void test_fmt_duration_zero_is_now(void) {
    char buf[32];
    fmt_duration(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("now", buf);
}

void test_fmt_duration_negative_is_now(void) {
    char buf[32];
    fmt_duration(-100, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("now", buf);
}

void test_fmt_duration_minutes_only(void) {
    char buf[32];
    fmt_duration(3599, buf, sizeof(buf));
    /* 3599 / 60 = 59 → "59m" */
    TEST_ASSERT_EQUAL_STRING("59m", buf);
}

void test_fmt_duration_days(void) {
    char buf[32];
    fmt_duration(90000, buf, sizeof(buf));
    /* 90000 / 86400 = 1d, (90000 % 86400) / 3600 = 1h → "1d 1h" */
    TEST_ASSERT_EQUAL_STRING("1d 1h", buf);
}

/* ── parse_reset_secs ────────────────────────────────────────────────────────
 * "now" reference: 2026-05-29T16:00:00Z = portable_timegm(2026,5,29,16,0,0)
 * Let's compute: days_from_civil(2026,5,29) then +16h
 *
 * We use a known reference: 2026-05-29T00:00:00Z as our anchor.
 * The exact epoch value is determined by portable_timegm, which we test implicitly.
 */

/* Compute the UTC epoch for 2026-05-29T16:00:00Z using the same algorithm */
static time_t ref_now() {
    return portable_timegm(2026, 5, 29, 16, 0, 0);
}

void test_parse_negative_offset(void) {
    /* "2026-05-29T12:00:00-05:00"
     * Wall clock 12:00 in UTC-5 = 17:00 UTC
     * now = 16:00 UTC
     * seconds until = 17:00 - 16:00 = 3600 */
    int32_t secs = parse_reset_secs("2026-05-29T12:00:00-05:00", ref_now());
    TEST_ASSERT_EQUAL_INT32(3600, secs);
}

void test_parse_past_returns_zero(void) {
    /* "2026-05-29T10:00:00Z" = 10:00 UTC, now = 16:00 UTC → past → 0 */
    int32_t secs = parse_reset_secs("2026-05-29T10:00:00Z", ref_now());
    TEST_ASSERT_EQUAL_INT32(0, secs);
}

void test_parse_z_suffix(void) {
    /* "2026-05-29T17:00:00Z" = 17:00 UTC, now = 16:00 UTC → 3600s */
    int32_t secs = parse_reset_secs("2026-05-29T17:00:00Z", ref_now());
    TEST_ASSERT_EQUAL_INT32(3600, secs);
}

void test_parse_null_returns_zero(void) {
    int32_t secs = parse_reset_secs(nullptr, ref_now());
    TEST_ASSERT_EQUAL_INT32(0, secs);
}

void test_parse_malformed_returns_zero(void) {
    int32_t secs = parse_reset_secs("not-a-date", ref_now());
    TEST_ASSERT_EQUAL_INT32(0, secs);
}

void test_parse_positive_offset(void) {
    /* "2026-05-29T18:00:00+01:00"
     * Wall clock 18:00 in UTC+1 = 17:00 UTC
     * now = 16:00 UTC → 3600s */
    int32_t secs = parse_reset_secs("2026-05-29T18:00:00+01:00", ref_now());
    TEST_ASSERT_EQUAL_INT32(3600, secs);
}

/* ── Main ─────────────────────────────────────────────────────────────────── */

int main(void) {
    UNITY_BEGIN();

    /* config tests (via test_config.cpp — PlatformIO compiles all files together) */
    /* These are registered in test_config.cpp via run_test_config() */
    extern void run_test_config(void);
    run_test_config();

    /* bmp tests */
    extern void run_test_bmp(void);
    run_test_bmp();

    /* touch tests */
    extern void run_test_touch(void);
    run_test_touch();

    /* claude tests */
    RUN_TEST(test_fmt_duration_hours_minutes);
    RUN_TEST(test_fmt_duration_zero_is_now);
    RUN_TEST(test_fmt_duration_negative_is_now);
    RUN_TEST(test_fmt_duration_minutes_only);
    RUN_TEST(test_fmt_duration_days);
    RUN_TEST(test_parse_negative_offset);
    RUN_TEST(test_parse_past_returns_zero);
    RUN_TEST(test_parse_z_suffix);
    RUN_TEST(test_parse_null_returns_zero);
    RUN_TEST(test_parse_malformed_returns_zero);
    RUN_TEST(test_parse_positive_offset);

    return UNITY_END();
}

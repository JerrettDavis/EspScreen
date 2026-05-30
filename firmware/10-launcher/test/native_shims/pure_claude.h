/**
 * pure_claude.h — Portable shims for Claude widget pure logic.
 *
 * Provides:
 *   fmt_duration(secs, buf, n)       — format seconds as human duration
 *   parse_reset_secs(iso, now)       — parse ISO-8601 timestamp → seconds until
 *
 * Design notes:
 *   - Uses sscanf for ISO-8601 parsing (NOT strptime — not available on MinGW/MSVC)
 *   - Implements a portable timegm replacement via Howard Hinnant's days_from_civil
 *   - All logic is inlined; no Arduino/LVGL/WiFi dependencies
 *   - 'now' is injected as a parameter for deterministic testing
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ── Portable UTC epoch from civil date (Howard Hinnant algorithm) ─────────
 * Returns the number of days since 1970-01-01 for the given year/month/day.
 * Works for any date in the range representable by int64_t.
 * Reference: https://howardhinnant.github.io/date_algorithms.html
 */
static inline int64_t days_from_civil(int y, int m, int d) {
    /* Shift year so March is month 0 (simplifies leap year calc) */
    if (m <= 2) { y -= 1; }
    /* Era: 400-year period */
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);          /* year-of-era [0, 399] */
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;  /* day-of-year [0, 365] */
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;             /* day-of-era [0, 146096] */
    return era * 146097 + (int64_t)doe - 719468;       /* days since 1970-01-01 */
}

/**
 * Portable replacement for timegm() — converts a UTC struct tm to time_t.
 * Does NOT consult the local timezone.
 */
static inline time_t portable_timegm(int year, int mon, int mday,
                                     int hour, int min, int sec) {
    /* mon is 1-based (1=Jan, 12=Dec) */
    int64_t days = days_from_civil(year, mon, mday);
    int64_t epoch = days * 86400LL + hour * 3600LL + min * 60LL + sec;
    return (time_t)epoch;
}

/**
 * Format a duration in seconds as a human-readable string.
 * Matches the logic in claude_widget.cpp::fmt_duration().
 *
 * Examples:
 *   7265  → "2h 1m"
 *   59    → "59m" (actually "0m" if <60s rounds to 0m — no: secs/60=0 so "0m")
 *   3599  → "59m"
 *   86400 → "1d 0h"
 *   0     → "now"
 *   -1    → "now"
 */
static inline void fmt_duration(int32_t secs, char* buf, size_t n) {
    if (secs <= 0) {
        snprintf(buf, n, "now");
    } else if (secs < 3600) {
        snprintf(buf, n, "%dm", (int)(secs / 60));
    } else if (secs < 86400) {
        snprintf(buf, n, "%dh %dm", (int)(secs / 3600), (int)((secs % 3600) / 60));
    } else {
        snprintf(buf, n, "%dd %dh", (int)(secs / 86400), (int)((secs % 86400) / 3600));
    }
}

/**
 * Parse an ISO-8601 timestamp and return seconds until the event relative to now.
 *
 * Supports:
 *   "YYYY-MM-DDTHH:MM:SS"           (naive — treated as UTC)
 *   "YYYY-MM-DDTHH:MM:SSZ"          (UTC)
 *   "YYYY-MM-DDTHH:MM:SS.frac Z"    (fractional seconds + Z)
 *   "YYYY-MM-DDTHH:MM:SS+HH:MM"     (positive offset)
 *   "YYYY-MM-DDTHH:MM:SS-HH:MM"     (negative offset)
 *
 * Returns 0 if ts is NULL, malformed, or the timestamp is in the past.
 * Injects 'now' (UTC epoch) as a parameter for deterministic testing.
 *
 * Note: uses sscanf instead of strptime for MinGW/MSVC portability.
 */
static inline int32_t parse_reset_secs(const char* ts, time_t now) {
    if (!ts) return 0;

    int year = 0, mon = 0, mday = 0, hour = 0, min = 0, sec = 0;
    int n = sscanf(ts, "%d-%d-%dT%d:%d:%d",
                   &year, &mon, &mday, &hour, &min, &sec);
    if (n != 6) return 0;

    /* Find position after "YYYY-MM-DDTHH:MM:SS" (19 chars) */
    const char* p = ts + 19;
    if (!*p) {
        /* Naive — no timezone specifier; treat as UTC */
        time_t target = portable_timegm(year, mon, mday, hour, min, sec);
        int32_t secs = (int32_t)(target - now);
        return secs < 0 ? 0 : secs;
    }

    /* Skip optional fractional seconds */
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') ++p;
    }

    int tz_offset_secs = 0;
    if (*p == 'Z' || *p == 'z') {
        tz_offset_secs = 0;
    } else if (*p == '+' || *p == '-') {
        int sign = (*p == '+') ? 1 : -1;
        ++p;
        int hh = 0, mm = 0;
        sscanf(p, "%d:%d", &hh, &mm);
        tz_offset_secs = sign * (hh * 3600 + mm * 60);
    }

    /* Convert parsed local time to UTC epoch */
    time_t local_epoch = portable_timegm(year, mon, mday, hour, min, sec);
    /* local_epoch is the UTC epoch of the "wall clock" time in the given TZ offset.
     * To get the actual UTC instant: subtract the offset.
     * E.g., 12:00:00-05:00 means UTC 17:00:00 → local_epoch(12:00:00) - (-5*3600) = +18000
     * Wait: offset -05:00 means TZ is UTC-5, so wall clock 12:00 = UTC 17:00.
     * UTC = wall_clock - tz_offset_secs  where -05:00 gives tz_offset_secs = -18000
     * UTC = 12:00_epoch - (-18000) = 12:00_epoch + 18000 ✓ */
    time_t target = local_epoch - tz_offset_secs;
    int32_t secs = (int32_t)(target - now);
    return secs < 0 ? 0 : secs;
}

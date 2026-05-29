#pragma once
#include <Arduino.h>

/**
 * Minimal logger — LOG_I/W/E macros → Serial with timestamp.
 * Format: [TAG LEVEL ts_ms] msg
 * A 20-line ring buffer is maintained for the About screen.
 */

void logger_init();
void logger_log(const char* level, const char* tag, const char* msg);

/* Convenience macros */
#define LOG_I(tag, msg, ...) do { \
    char _buf[128]; snprintf(_buf, sizeof(_buf), msg, ##__VA_ARGS__); \
    logger_log("I", tag, _buf); } while(0)

#define LOG_W(tag, msg, ...) do { \
    char _buf[128]; snprintf(_buf, sizeof(_buf), msg, ##__VA_ARGS__); \
    logger_log("W", tag, _buf); } while(0)

#define LOG_E(tag, msg, ...) do { \
    char _buf[128]; snprintf(_buf, sizeof(_buf), msg, ##__VA_ARGS__); \
    logger_log("E", tag, _buf); } while(0)

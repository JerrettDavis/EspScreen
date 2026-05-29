#include "logger.h"
#include <Arduino.h>
#include <string.h>

/**
 * Logger — writes to Serial with format: [TAG LEVEL ts_ms] msg
 * Maintains a 20-line ring buffer for the About screen.
 */

#define RING_SIZE    20
#define LOG_LINE_MAX 128

static char s_ring[RING_SIZE][LOG_LINE_MAX];
static int  s_head = 0;
static int  s_count = 0;
static bool s_ready = false;

void logger_init() {
    memset(s_ring, 0, sizeof(s_ring));
    s_head  = 0;
    s_count = 0;
    s_ready = true;
}

void logger_log(const char* level, const char* tag, const char* msg) {
    char line[LOG_LINE_MAX];
    snprintf(line, sizeof(line), "[%s %s %lu] %s", tag, level, (unsigned long)millis(), msg);

    /* Write to Serial */
    Serial.println(line);

    /* Store in ring buffer */
    if (s_ready) {
        strncpy(s_ring[s_head], line, LOG_LINE_MAX - 1);
        s_ring[s_head][LOG_LINE_MAX - 1] = '\0';
        s_head = (s_head + 1) % RING_SIZE;
        if (s_count < RING_SIZE) s_count++;
    }
}

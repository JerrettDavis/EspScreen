#include "sd_store.h"
#include "spi_bus.h"
#include "logger.h"
#include "../../../shared/pinmap.h"
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

/**
 * sd_store — SD card on a dedicated VSPI bus (SCK=18, MISO=19, MOSI=23, CS=5).
 *
 * Board: Sunton ESP32-3248S035 / Cheap Yellow Display 3.5" ST7796 variant.
 *
 * CRITICAL SPI wiring notes:
 *   - The display (ST7796) is on HSPI: SCLK=14, MOSI=13, MISO=12, CS=15.
 *   - The SD card is on a SEPARATE VSPI bus: SCK=18, MISO=19, MOSI=23, CS=5.
 *   - We create our own SPIClass(VSPI) instance s_sd_spi and begin() it on
 *     the SD pins.  We do NOT touch TFT_eSPI's bus at all.
 *   - This eliminates all shared-bus contention and "Select Failed" errors
 *     that occurred when the old code clocked the wrong (display) pins.
 *
 * SD library: bundled with arduino-esp32 (no external lib_deps entry).
 */

static bool     s_mounted = false;
static SPIClass s_sd_spi(VSPI);

/* Token-cache and directory paths */
static constexpr const char* kSdDir       = "/espscreen";
static constexpr const char* kTokenPath   = "/espscreen/tokens.json";

namespace sd_store {

bool init() {
    /* SD is on its own VSPI bus (SCK=18, MISO=19, MOSI=23, CS=5).
     * Drive CS HIGH immediately so the card does not see a spurious
     * assertion while the bus is being initialised. */
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    /* Brief settling delay before the first probe. */
    delay(20);

    /* Start the dedicated VSPI bus on the SD pins.
     * This is completely independent of TFT_eSPI's HSPI bus. */
    s_sd_spi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

    /* Retry loop: first attempt at SD_SCK_HZ (20 MHz); fall back to 4 MHz
     * on subsequent attempts (some cards are slow to power-up).
     *
     * spi_bus::lock/unlock are kept as harmless no-ops — they guard the
     * display HSPI bus, not this VSPI bus.  Keeping them avoids having to
     * audit every call-site and costs nothing on a cooperative single-loop
     * system. */
    bool ok = false;
    const uint32_t speeds[] = { SD_SCK_HZ, 4000000UL };
    for (int attempt = 1; attempt <= 3 && !ok; ++attempt) {
        /* Toggle CS to reset any partially-clocked card state. */
        digitalWrite(PIN_SD_CS, LOW);
        delayMicroseconds(10);
        digitalWrite(PIN_SD_CS, HIGH);
        delay(5);

        uint32_t hz = speeds[(attempt > 1) ? 1 : 0];  // slow fallback from attempt 2

        if (!spi_bus::lock(500)) {
            LOG_W("sd", "init: bus lock timeout (attempt %d)", attempt);
            break;
        }
        SD.end();   // no-op on first attempt; cleans up on retry
        ok = SD.begin(PIN_SD_CS, s_sd_spi, hz);
        spi_bus::unlock();

        if (!ok) {
            LOG_W("sd", "SD.begin attempt %d failed (hz=%lu)", attempt, (unsigned long)hz);
            delay(50);
        }
    }

    if (!ok) {
        s_mounted = false;
        LOG_I("sd", "no card - NVS/LittleFS only");
        return false;
    }

    s_mounted = true;

    /* Create the working directory if it does not exist yet. */
    if (!SD.exists(kSdDir)) {
        SD.mkdir(kSdDir);
    }

    LOG_I("sd", "mounted, size=%lluMB", (uint64_t)SD.cardSize() / (1024 * 1024));
    LOG_I("sd", "mounted, free heap=%lu", (unsigned long)esp_get_free_heap_size());
    return true;
}

bool available() {
    return s_mounted;
}

bool exists(const char* path) {
    if (!s_mounted) return false;
    if (!spi_bus::lock()) return false;
    bool result = SD.exists(path);
    spi_bus::unlock();
    return result;
}

bool read_file(const char* path, String& out) {
    if (!s_mounted) return false;
    if (!spi_bus::lock()) return false;

    File f = SD.open(path, FILE_READ);
    if (!f) {
        spi_bus::unlock();
        LOG_W("sd", "read_file: cannot open %s", path);
        return false;
    }

    size_t sz = f.size();
    if (sz > 8192) {
        f.close();
        spi_bus::unlock();
        LOG_W("sd", "read_file: %s too large (%u)", path, (unsigned)sz);
        return false;
    }
    out = "";
    out.reserve(sz);
    uint8_t buf[256];
    while (f.available()) {
        size_t n = f.read(buf, sizeof(buf));
        out.concat((const char*)buf, n);
    }
    f.close();
    spi_bus::unlock();
    return true;
}

bool write_file(const char* path, const String& data) {
    if (!s_mounted) return false;
    if (!spi_bus::lock()) return false;

    /* Build a .tmp path beside the target */
    String tmp_path = String(path) + ".tmp";

    File f = SD.open(tmp_path.c_str(), FILE_WRITE);
    if (!f) {
        spi_bus::unlock();
        LOG_W("sd", "write_file: cannot open tmp %s", tmp_path.c_str());
        return false;
    }
    f.print(data);
    f.close();

    /* Atomic rename: remove old target then rename tmp */
    if (SD.exists(path)) {
        SD.remove(path);
    }
    bool ok = SD.rename(tmp_path.c_str(), path);
    spi_bus::unlock();

    if (!ok) {
        LOG_W("sd", "write_file: rename failed for %s", path);
    }
    return ok;
}

bool write_token_cache(const String& json) {
    return write_file(kTokenPath, json);
}

bool read_token_cache(String& out) {
    return read_file(kTokenPath, out);
}

} // namespace sd_store

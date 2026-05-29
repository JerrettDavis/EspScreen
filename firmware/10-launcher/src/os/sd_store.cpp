#include "sd_store.h"
#include "spi_bus.h"
#include "logger.h"
#include "../../../shared/pinmap.h"
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>

/**
 * sd_store — SD card on the shared SPI bus (SCLK=14, MOSI=13, MISO=12).
 *
 * CRITICAL SPI wiring notes:
 *   - The board uses SCLK=14/MOSI=13/MISO=12 for ALL SPI peripherals.
 *   - TFT_eSPI (CONFIG_IDF_TARGET_ESP32, no USE_HSPI_PORT) creates its own
 *     SPIClass(VSPI) instance.  We MUST obtain that same instance via
 *     TFT_eSPI::getSPIinstance() and pass it to SD.begin() — never call
 *     the single-arg SD.begin(PIN_SD_CS) which would re-init SPI on the
 *     default (VSPI) pins and clobber the display bus state.
 *   - PIN_SD_CS is driven HIGH before SD.begin() so a floating CS line
 *     cannot corrupt TFT frames during the probe.
 *
 * SD library: bundled with arduino-esp32 (no external lib_deps entry).
 */

static bool s_mounted = false;

/* Token-cache and directory paths */
static constexpr const char* kSdDir       = "/espscreen";
static constexpr const char* kTokenPath   = "/espscreen/tokens.json";

namespace sd_store {

bool init() {
    /* Drive ALL SPI CS lines HIGH before touching the bus.
     *
     * Rationale: after display and touch init the TFT/touch CS lines may
     * be floating or in an indeterminate state.  If either CS is asserted
     * low while we probe the SD, the bus will be corrupted and the SD card
     * will not respond.  Explicitly drive all three CS lines HIGH here so
     * only our SD CS transitions during SD.begin.
     *
     * PIN_TFT_CS=15, PIN_TOUCH_CS=33, PIN_SD_CS=5 (from pinmap.h).
     * TFT and touch are already configured as outputs by their drivers;
     * we still call pinMode to be safe in case ordering changes.
     */
    pinMode(PIN_TFT_CS,   OUTPUT); digitalWrite(PIN_TFT_CS,   HIGH);
    pinMode(PIN_TOUCH_CS, OUTPUT); digitalWrite(PIN_TOUCH_CS, HIGH);
    pinMode(PIN_SD_CS,    OUTPUT); digitalWrite(PIN_SD_CS,    HIGH);

    /* Brief settling delay: let the bus stabilise after display/touch init
     * before the SD probe drives CS low for the first time. */
    delay(20);

    /* Obtain the SPIClass that TFT_eSPI already initialised.
     * TFT_eSPI creates SPIClass(VSPI) internally (ESP32 default); we
     * must reuse that exact instance — never start a second one. */
    SPIClass& shared_spi = TFT_eSPI::getSPIinstance();

    /* SD.begin probes the bus — must hold the arbiter lock so the display
     * CS line cannot be asserted simultaneously by another caller.
     *
     * Clock speed: 20 MHz was too aggressive on a shared bus with display
     * stubs.  Use SD_SCK_HZ (now 4 MHz) for reliable card initialisation.
     * The card negotiates its own higher speed after GO_IDLE / CMD0.
     *
     * Retry: some cards need a second probe attempt after the bus settles
     * following display init.  Try up to 3 times with a CS-toggle reset
     * between attempts.
     */
    bool ok = false;
    for (int attempt = 1; attempt <= 3 && !ok; ++attempt) {
        /* Toggle CS low then high to reset any partially-clocked card state
         * before each attempt. */
        digitalWrite(PIN_SD_CS, LOW);
        delayMicroseconds(10);
        digitalWrite(PIN_SD_CS, HIGH);
        delay(5);

        if (!spi_bus::lock(500)) {
            LOG_W("sd", "init: bus lock timeout (attempt %d)", attempt);
            break;
        }
        SD.end();                                           // no-op on first attempt; cleans up on retry
        ok = SD.begin(PIN_SD_CS, shared_spi, SD_SCK_HZ);
        spi_bus::unlock();

        if (!ok) {
            LOG_W("sd", "SD.begin attempt %d failed", attempt);
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

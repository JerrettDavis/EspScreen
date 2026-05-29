#include "spi_bus.h"
#include "logger.h"
#include <Arduino.h>

/**
 * spi_bus — cooperative SPI bus arbiter.
 *
 * s_locked is volatile because it is written and read from loop() code
 * paths; the volatile ensures the compiler does not cache the value in
 * a register across the spin loop in lock().
 *
 * No ISR touches this flag, so volatile (not atomic) is sufficient.
 */
static volatile bool s_locked = false;

namespace spi_bus {

void init() {
    // The SPIClass is already started by TFT_eSPI inside display::init().
    // Nothing to start here — just confirm we are ready.
    s_locked = false;
    LOG_I("spi_bus", "init: cooperative arbiter ready");
}

bool lock(uint32_t timeout_ms) {
    uint32_t deadline = millis() + timeout_ms;
    while (s_locked) {
        if ((int32_t)(millis() - deadline) >= 0) {
            LOG_W("spi_bus", "lock timeout after %lums", (unsigned long)timeout_ms);
            return false;
        }
        yield(); // keep WDT happy while spinning
    }
    s_locked = true;
    return true;
}

void unlock() {
    s_locked = false;
}

bool is_locked() {
    return s_locked;
}

} // namespace spi_bus

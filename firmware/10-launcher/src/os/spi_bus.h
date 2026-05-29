#pragma once
#include <stdint.h>

/**
 * spi_bus — cooperative SPI bus arbiter for shared-bus peripherals.
 *
 * There is exactly ONE SPIClass instance on this board, owned by TFT_eSPI.
 * Display (ST7796), touch (XPT2046), and SD card all share SCLK/MOSI/MISO
 * (GPIO 14/13/12). Each peripheral has its own CS line.
 *
 * All SPI activity runs on the Arduino main loop — no RTOS tasks, no
 * real mutex needed. This is a thin cooperative guard: callers must hold
 * the lock while their CS is asserted and release it immediately after.
 *
 * SD_SCK_HZ is defined here so sd_store.cpp and any future peripheral
 * driver can reference the agreed bus speed without magic numbers.
 */

// SD card SPI clock.
// 20 MHz proved unreliable on this shared bus (TFT CS stub + display
// already active at init time caused sdSelectCard Select Failed / FR_NOT_READY).
// 4 MHz is the safe value that gets the card through CMD0/GO_IDLE reliably.
// The SD library negotiates higher speed after successful ACMD41, but we
// lock the bus clock here and let the card do what it can.
// If 4 MHz mounts reliably and performance is needed, try 10 MHz first,
// then 20 MHz — but only after confirming the 4 MHz baseline is solid.
static constexpr uint32_t SD_SCK_HZ = 4000000UL;

namespace spi_bus {

    /**
     * init() — must be called once, AFTER display::init() has already
     * brought up the TFT_eSPI SPIClass instance.
     */
    void init();

    /**
     * lock() — claim the bus for a non-display SPI operation.
     * Returns true immediately if the bus is free.
     * Spins (yield loop) for up to timeout_ms milliseconds if busy.
     * Returns false on timeout — caller should abort the operation.
     *
     * On a cooperative single-loop system this should never contend;
     * the timeout is purely a safety net for future code.
     */
    bool lock(uint32_t timeout_ms = 100);

    /** unlock() — release the bus.  Call after asserting CS high. */
    void unlock();

    /** is_locked() — true while a non-display peripheral holds the bus. */
    bool is_locked();

} // namespace spi_bus

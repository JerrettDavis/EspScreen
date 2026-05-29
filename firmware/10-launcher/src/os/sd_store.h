#pragma once
#include <Arduino.h>

/**
 * sd_store — SD card access on the shared SPI bus.
 *
 * All operations are wrapped in spi_bus::lock()/unlock() so they
 * cooperate safely with TFT_eSPI flush callbacks on the same loop.
 *
 * Paths on the SD card use the prefix /espscreen/ by convention.
 * The card is mounted at the root ("/sd" internally; paths passed
 * to SD library functions use that mount point prefix automatically).
 *
 * init() is safe to call when no card is inserted — it logs a notice
 * and returns false; all subsequent operations return false gracefully.
 */

namespace sd_store {

    /**
     * init() — attempt to mount the SD card on the shared SPI bus.
     * Call AFTER spi_bus::init() and display::init().
     * Returns true if a card was found and mounted.
     * Returns false if no card present — system continues with LittleFS only.
     * Never blocks or crashes on absent card.
     */
    bool init();

    /** available() — true if the SD card is currently mounted. */
    bool available();

    /** exists() — true if the given path exists on the SD card. */
    bool exists(const char* path);

    /**
     * read_file() — read the entire file at path into out.
     * Returns true on success.  out is untouched on failure.
     */
    bool read_file(const char* path, String& out);

    /**
     * write_file() — atomically write data to path.
     * Writes to a .tmp file first, then renames over the target.
     * Returns true if the final file was written successfully.
     */
    bool write_file(const char* path, const String& data);

    /**
     * write_token_cache() — write json to /espscreen/tokens.json.
     * Convenience wrapper around write_file().
     */
    bool write_token_cache(const String& json);

    /**
     * read_token_cache() — read /espscreen/tokens.json into out.
     * Convenience wrapper around read_file().
     */
    bool read_token_cache(String& out);

} // namespace sd_store

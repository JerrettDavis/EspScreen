#pragma once

namespace recovery {
    /**
     * Check for factory-reset triggers before LVGL initialises.
     * Call immediately after Serial.begin() + delay(100) in setup().
     *
     * Two hatches:
     *   A — GPIO0 (BOOT button) held LOW for ~3 s during boot.
     *   B — Type "reset" on Serial within the first 5 s of boot.
     *
     * Either hatch erases NVS, formats LittleFS, and restarts the board.
     * If neither hatch is triggered, returns normally and boot continues.
     */
    void check();
}

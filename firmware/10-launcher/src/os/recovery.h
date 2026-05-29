#pragma once

namespace recovery {
    /**
     * Check for factory-reset and calibration triggers before LVGL initialises.
     * Call immediately after Serial.begin() + delay(100) in setup().
     *
     * Hatches:
     *   A — GPIO0 (BOOT button) held LOW for ~3 s during boot → factory reset.
     *   B — Type "reset" on Serial within the first 5 s → factory reset.
     *   C — Type "cal" on Serial within the first 5 s → sets pending_cal flag.
     *
     * Factory reset erases NVS, formats LittleFS, and restarts the board.
     * Cal flag is checked by main.cpp after LVGL init via pending_cal().
     * If neither hatch is triggered, returns normally and boot continues.
     */
    void check();

    /**
     * Returns true (and clears the flag) if 'cal' was typed during the
     * recovery window. Call once after LVGL init, before loading the launcher.
     */
    bool pending_cal();

    /**
     * Set the pending_cal flag from outside the recovery window
     * (e.g., serial command in main loop). Survives until consumed by pending_cal().
     */
    void request_cal();
}

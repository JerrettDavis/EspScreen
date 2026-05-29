#pragma once
#include <lvgl.h>

namespace calibrate {
    /**
     * Create and push the hold-to-settle calibration screen.
     * Safe to call from anywhere after LVGL is initialised.
     * On success: saves cal to NVS, applies immediately via touch::apply_cal(),
     *             then pops back to the previous screen.
     * On cancel/timeout: discards samples, pops back, Phase 0 defaults remain.
     */
    void launch();

    /**
     * Factory for screen_router if wired as a tile.
     * Equivalent to calling launch() and returning the screen object.
     */
    lv_obj_t* create_screen();
}

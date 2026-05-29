#include "theme.h"
#include <lvgl.h>

namespace ui_theme {

void apply() {
    lv_display_t* disp = lv_display_get_default();
    if (!disp) return;

    /* LVGL 9: apply default dark theme */
    lv_theme_t* th = lv_theme_default_init(
        disp,
        lv_palette_main(LV_PALETTE_BLUE),     /* primary */
        lv_palette_main(LV_PALETTE_CYAN),      /* secondary */
        true,                                   /* dark mode */
        &lv_font_montserrat_14
    );
    lv_display_set_theme(disp, th);
}

} // namespace ui_theme

#include "calculator_stub.h"
#include "../../os/screen_router.h"
#include "../../ui/widgets.h"
#include "../../ui/theme.h"
#include "../../ui/tokens.h"
#include <lvgl.h>

namespace calculator_stub {

static void back_cb(lv_event_t* e) { screen_router::pop(); }

lv_obj_t* create_screen() {
    lv_obj_t* scr = widgets::make_screen();
    widgets::make_topbar(scr, "Calculator", back_cb);

    lv_obj_t* lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "Coming soon");
    lv_obj_add_style(lbl, ui_theme::style_text_key(), 0);
    lv_obj_set_parent(lbl, scr);
    lv_obj_center(lbl);

    return scr;
}

} // namespace calculator_stub

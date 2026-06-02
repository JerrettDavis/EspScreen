#include "about.h"
#include "../../os/screen_router.h"
#include "../../os/net_manager.h"
#include "../../os/config.h"
#include "../../ui/widgets.h"
#include "../../ui/theme.h"
#include "../../ui/tokens.h"
#include <Arduino.h>
#include <lvgl.h>

namespace about {

static void back_cb(lv_event_t* e) {
    screen_router::pop();
}

lv_obj_t* create_screen() {
    lv_obj_t* scr = widgets::make_screen();
    widgets::make_topbar(scr, "About", back_cb);
    screen_router::attach_autodelete(scr);

    /* Card with key-value rows */
    lv_obj_t* card = widgets::make_card(scr, -1, -1);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, tok::TOPBAR_H + tok::SP_L);

    /* Version */
    lv_obj_t* version_val = widgets::make_kv_row(card, "Version");
    lv_label_set_text(version_val, ESPSCREEN_VERSION);

    /* Phase */
    lv_obj_t* phase_val = widgets::make_kv_row(card, "Phase");
    char phase_str[16];
    snprintf(phase_str, sizeof(phase_str), "%d", ESPSCREEN_PHASE);
    lv_label_set_text(phase_val, phase_str);

    /* Free heap */
    lv_obj_t* heap_val = widgets::make_kv_row(card, "Free heap");
    char heap_str[32];
    snprintf(heap_str, sizeof(heap_str), "%lu B", (unsigned long)esp_get_free_heap_size());
    lv_label_set_text(heap_val, heap_str);

    /* IP address */
    lv_obj_t* ip_val = widgets::make_kv_row(card, "IP address");
    lv_label_set_text(ip_val, net_manager::ip_string().c_str());

    /* Hostname */
    lv_obj_t* hostname_val = widgets::make_kv_row(card, "Hostname");
    lv_label_set_text(hostname_val, config::network().hostname);

    return scr;
}

} // namespace about

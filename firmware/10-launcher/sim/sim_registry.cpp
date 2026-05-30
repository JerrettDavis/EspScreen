/**
 * sim_registry.cpp — full list of renderable screens (Phase 2: 9 screens).
 *
 * Each entry wraps a REAL screen factory in a plain C thunk so the registry is
 * a POD usable from both C and C++. The settings sub-screens use the sim-only
 * accessors exposed (under ESPSCREEN_SIM) at the end of settings.cpp.
 */
#include "sim_registry.h"

#include "../src/app/builtin/launcher.h"
#include "../src/app/builtin/settings.h"
#include "../src/app/builtin/about.h"
#include "../src/app/builtin/touch_test.h"
#include "../src/app/builtin/claude_widget.h"
#include "../src/app/builtin/calculator_stub.h"
#include "../src/app/builtin/wifi_setup.h"
#include "sim_screens.h"   /* settings::sim_claude_profiles_screen / sim_wifi_networks_screen */

static lv_obj_t* mk_launcher(void)         { return launcher::create_screen(); }
static lv_obj_t* mk_settings(void)         { return settings::create_screen(); }
static lv_obj_t* mk_claude_profiles(void)  { return settings::sim_claude_profiles_screen(); }
static lv_obj_t* mk_wifi_networks(void)    { return settings::sim_wifi_networks_screen(); }
static lv_obj_t* mk_about(void)            { return about::create_screen(); }
static lv_obj_t* mk_touch_test(void)       { return touch_test::create_screen(); }
static lv_obj_t* mk_claude_widget(void)    { return claude_widget::create_screen(); }
static lv_obj_t* mk_calculator(void)       { return calculator_stub::create_screen(); }
static lv_obj_t* mk_wifi_setup(void)       { return wifi_setup::create_screen(); }

const sim_screen_t sim_registry[] = {
    { "launcher",         mk_launcher        },
    { "settings",         mk_settings        },
    { "claude_profiles",  mk_claude_profiles },
    { "wifi_networks",    mk_wifi_networks   },
    { "about",            mk_about           },
    { "touch_test",       mk_touch_test      },
    { "claude_widget",    mk_claude_widget   },
    { "calculator",       mk_calculator      },
    { "wifi_setup",       mk_wifi_setup      },
};

const int sim_registry_count =
    (int)(sizeof(sim_registry) / sizeof(sim_registry[0]));

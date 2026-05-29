#include "app_registry.h"
#include "builtin/settings.h"
#include "builtin/touch_test.h"
#include "builtin/about.h"
#include "builtin/claude_widget.h"
#include "builtin/calculator_stub.h"
// calibrate.h intentionally excluded — cal is disabled (touch uses factory defaults)

namespace app_registry {

const AppEntry kBuiltinApps[] = {
    { "settings",      "Settings",      LV_SYMBOL_SETTINGS,  settings::create_screen       },
    { "touch_test",    "Touch Test",    LV_SYMBOL_OK,        touch_test::create_screen     },
    { "about",         "About",         LV_SYMBOL_LIST,      about::create_screen          },
    { "claude",        "Claude Usage",  LV_SYMBOL_CHARGE,    claude_widget::create_screen  },
    { "calculator",    "Calculator",    LV_SYMBOL_EDIT,      calculator_stub::create_screen },
};

const int kBuiltinAppCount = sizeof(kBuiltinApps) / sizeof(kBuiltinApps[0]);

} // namespace app_registry

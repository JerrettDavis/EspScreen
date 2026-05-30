#include "app_registry.h"
#include "builtin/settings.h"
#include "builtin/touch_test.h"
#include "builtin/about.h"
#include "builtin/claude_widget.h"
#include "builtin/calculator_stub.h"
// calibrate.h intentionally excluded — cal is disabled (touch uses factory defaults)

namespace app_registry {

/* Icons are short ASCII mnemonics (in Basic-Latin font range), NOT LV_SYMBOL_*
 * FontAwesome glyphs — those render as tofu boxes on this panel. */
const AppEntry kBuiltinApps[] = {
    { "settings",      "Settings",      "S",  settings::create_screen       },
    { "touch_test",    "Touch Test",    "T",  touch_test::create_screen     },
    { "about",         "About",         "i",  about::create_screen          },
    { "claude",        "Claude Usage",  "C",  claude_widget::create_screen  },
    { "calculator",    "Calculator",    "=",  calculator_stub::create_screen },
};

const int kBuiltinAppCount = sizeof(kBuiltinApps) / sizeof(kBuiltinApps[0]);

} // namespace app_registry

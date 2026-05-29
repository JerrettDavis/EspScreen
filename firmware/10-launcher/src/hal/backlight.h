#pragma once
#include <stdint.h>

namespace backlight {
    void init(uint8_t pct = 80);       // set up PWM, initial brightness
    void set_pct(uint8_t pct);         // 0–100
    void dim(uint8_t pct = 20);        // idle dim
    void restore();                    // restore to configured level
}

#pragma once
#include <Arduino.h>

namespace time_sync {
    void begin();          // start NTP (Stage 1e)
    bool is_synced();
    String time_str();     // "HH:MM"
    String date_str();     // "YYYY-MM-DD"
}

#pragma once

namespace wifi_mgr {
    /**
     * Attempt connection with NVS-stored credentials.
     * If no SSID stored, or connection fails after 10 s,
     * starts captive portal AP "EspScreen-Setup" (Stage 1d).
     */
    void begin();
    bool is_connected();
    const char* ip_str();
}

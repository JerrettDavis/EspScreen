/**
 * net_manager_stub.cpp — host stub for the WiFi/portal state machine.
 *
 * settings.cpp conditionally uses net_manager (guarded by __has_include
 * "os/net_manager.h", which IS present), so it references mode()/ap_ssid().
 * Report a stable "connected" mode so settings renders the StaConnected branch.
 */
#include "../../src/os/net_manager.h"

namespace net_manager {
    void        init() {}
    void        loop() {}
    Mode        mode() { return Mode::StaConnected; }
    const char* ap_ssid() { return "sim-net"; }
    String      ip_string() { return String("192.168.1.42"); }
    void        force_portal() {}
    bool        last_retry_failed() { return false; }
    void        _trigger_sta_retry(const char*, const char*) {}
}

/**
 * claude_auth_stub.cpp — host stub for the OAuth profile store.
 *
 * Returns one fake configured profile so settings/claude_widget render their
 * "configured" branches. Network-path symbols (refresh_active, get_active_access_token)
 * are defined but the harness never invokes the code that calls them.
 */
#include "../../src/os/claude_auth.h"
#include <cstring>
#include <cstdio>

namespace claude_auth {
    void    init() {}
    uint8_t profile_count() { return 1; }
    uint8_t active_index()  { return 0; }

    bool load_profile(uint8_t idx, Profile& out) {
        if (idx != 0) return false;
        out.index = 0;
        std::snprintf(out.label, sizeof(out.label), "sim");
        out.access[0]  = '\0';
        out.refresh[0] = '\0';
        out.expires_at_ms = 0;
        return true;
    }
    uint8_t find_by_label(const char*) { return 255; }
    uint8_t add_profile(const char*)   { return 255; }
    bool    remove_profile(uint8_t)    { return false; }
    bool    set_tokens(uint8_t, const char*, const char*, int64_t) { return false; }
    bool    set_active(uint8_t) { return true; }

    String  get_active_access_token(bool* is_expired) {
        if (is_expired) *is_expired = false;
        return String("");
    }
    String  get_active_label() { return String("sim"); }
    bool    is_token_expired() { return false; }
    bool    wall_clock_ready() { return true; }
    bool    refresh_active()   { return false; }
    bool    set_tokens_by_label(const char*, const char*, const char*, int64_t) { return false; }
}

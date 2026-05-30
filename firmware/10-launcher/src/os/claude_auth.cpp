/**
 * claude_auth.cpp — Multi-profile OAuth token storage for Claude.
 *
 * NVS namespace "claude": count (u8), active (u8)
 * NVS namespace "cl_p<N>": label (str), access (str), refresh (str),
 *                           exphi (i32 = high 32 bits of ms), explo (i32 = low 32 bits of ms)
 *
 * Note: ESP32 Preferences doesn't support int64 natively.
 * We store expiresAt (ms) as two int32 values: exphi (bits 63-32) and explo (bits 31-0).
 */

#include "claude_auth.h"
#include "nvs_store.h"
#include "logger.h"
#include <Arduino.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace claude_auth {

/* ── NVS helpers ─────────────────────────────────────────────────────── */

static const char* NS_GLOBAL  = "claude";
static const char* KEY_COUNT  = "count";
static const char* KEY_ACTIVE = "active";

/* Build per-profile namespace string: "cl_p0" … "cl_p3" */
static void profile_ns(uint8_t idx, char* buf, size_t n) {
    snprintf(buf, n, "cl_p%u", (unsigned)idx);
}

/* Store int64 as two int32 in NVS */
static void put_i64(const char* ns, const char* key_hi, const char* key_lo, int64_t val) {
    int32_t hi = (int32_t)(val >> 32);
    int32_t lo = (int32_t)(val & 0xFFFFFFFF);
    nvs_store::put_i32(ns, key_hi, hi);
    nvs_store::put_i32(ns, key_lo, lo);
}

static int64_t get_i64(const char* ns, const char* key_hi, const char* key_lo) {
    int32_t hi = nvs_store::get_i32(ns, key_hi, 0);
    int32_t lo = nvs_store::get_i32(ns, key_lo, 0);
    return ((int64_t)hi << 32) | (uint32_t)lo;
}

/* ── Public API ──────────────────────────────────────────────────────── */

void init() {
    uint8_t count = nvs_store::get_u8(NS_GLOBAL, KEY_COUNT, 0);
    uint8_t active = nvs_store::get_u8(NS_GLOBAL, KEY_ACTIVE, 0);
    LOG_I("claude_auth", "init: %u profile(s), active=%u", count, active);

    /* Sanity: clamp active to valid range */
    if (count == 0) {
        nvs_store::put_u8(NS_GLOBAL, KEY_ACTIVE, 0);
    } else if (active >= count) {
        nvs_store::put_u8(NS_GLOBAL, KEY_ACTIVE, 0);
        LOG_W("claude_auth", "active index out of range, reset to 0");
    }
}

uint8_t profile_count() {
    return nvs_store::get_u8(NS_GLOBAL, KEY_COUNT, 0);
}

uint8_t active_index() {
    return nvs_store::get_u8(NS_GLOBAL, KEY_ACTIVE, 0);
}

bool load_profile(uint8_t idx, Profile& out) {
    if (idx >= profile_count()) return false;
    char ns[8];
    profile_ns(idx, ns, sizeof(ns));
    out.index = idx;
    String lbl = nvs_store::get_str(ns, "label", "");
    strlcpy(out.label, lbl.c_str(), sizeof(out.label));
    String acc = nvs_store::get_str(ns, "access", "");
    strlcpy(out.access, acc.c_str(), sizeof(out.access));
    String ref = nvs_store::get_str(ns, "refresh", "");
    strlcpy(out.refresh, ref.c_str(), sizeof(out.refresh));
    out.expires_at_ms = get_i64(ns, "exphi", "explo");
    return true;
}

uint8_t find_by_label(const char* label) {
    uint8_t count = profile_count();
    for (uint8_t i = 0; i < count; i++) {
        char ns[8];
        profile_ns(i, ns, sizeof(ns));
        String lbl = nvs_store::get_str(ns, "label", "");
        if (lbl.equalsIgnoreCase(label)) return i;
    }
    return 255;
}

uint8_t add_profile(const char* label) {
    uint8_t count = profile_count();
    if (count >= MAX_PROFILES) {
        LOG_W("claude_auth", "add_profile: max profiles reached (%u)", MAX_PROFILES);
        return 255;
    }
    /* Check for duplicate label */
    if (find_by_label(label) != 255) {
        LOG_W("claude_auth", "add_profile: label '%s' already exists", label);
        return 255;
    }
    char ns[8];
    profile_ns(count, ns, sizeof(ns));
    nvs_store::put_str(ns, "label", label);
    nvs_store::put_str(ns, "access", "");
    nvs_store::put_str(ns, "refresh", "");
    put_i64(ns, "exphi", "explo", 0);
    nvs_store::put_u8(NS_GLOBAL, KEY_COUNT, count + 1);
    if (count == 0) {
        /* First profile becomes active automatically */
        nvs_store::put_u8(NS_GLOBAL, KEY_ACTIVE, 0);
    }
    LOG_I("claude_auth", "add_profile: '%s' → index %u", label, count);
    return count;
}

bool remove_profile(uint8_t idx) {
    uint8_t count = profile_count();
    if (idx >= count) return false;

    /* Shift profiles down by re-writing each from idx+1 onward */
    for (uint8_t i = idx; i + 1 < count; i++) {
        char src_ns[8], dst_ns[8];
        profile_ns(i + 1, src_ns, sizeof(src_ns));
        profile_ns(i, dst_ns, sizeof(dst_ns));
        /* Copy all keys from src → dst */
        String lbl = nvs_store::get_str(src_ns, "label", "");
        String acc = nvs_store::get_str(src_ns, "access", "");
        String ref = nvs_store::get_str(src_ns, "refresh", "");
        int64_t exp = get_i64(src_ns, "exphi", "explo");
        nvs_store::put_str(dst_ns, "label", lbl.c_str());
        nvs_store::put_str(dst_ns, "access", acc.c_str());
        nvs_store::put_str(dst_ns, "refresh", ref.c_str());
        put_i64(dst_ns, "exphi", "explo", exp);
    }

    /* Clear the now-vacated last slot */
    char last_ns[8];
    profile_ns(count - 1, last_ns, sizeof(last_ns));
    nvs_store::put_str(last_ns, "label", "");
    nvs_store::put_str(last_ns, "access", "");
    nvs_store::put_str(last_ns, "refresh", "");
    put_i64(last_ns, "exphi", "explo", 0);

    uint8_t new_count = count - 1;
    nvs_store::put_u8(NS_GLOBAL, KEY_COUNT, new_count);

    /* Adjust active index */
    uint8_t active = active_index();
    if (new_count == 0) {
        nvs_store::put_u8(NS_GLOBAL, KEY_ACTIVE, 0);
    } else if (active >= new_count) {
        nvs_store::put_u8(NS_GLOBAL, KEY_ACTIVE, new_count - 1);
    } else if (active > idx) {
        nvs_store::put_u8(NS_GLOBAL, KEY_ACTIVE, active - 1);
    }

    LOG_I("claude_auth", "remove_profile: removed index %u, new count=%u", idx, new_count);
    return true;
}

bool set_tokens(uint8_t idx, const char* access, const char* refresh_tok, int64_t expires_unix) {
    if (idx >= profile_count()) return false;
    char ns[8];
    profile_ns(idx, ns, sizeof(ns));

    nvs_store::put_str(ns, "access", access);
    nvs_store::put_str(ns, "refresh", refresh_tok);

    /* Auto-detect ms vs sec: Unix timestamps in ms are > 1e11 (year 1973+) */
    int64_t expires_ms;
    if (expires_unix == 0) {
        /* Unknown — assume 1 hour from now */
        expires_ms = (int64_t)millis() + 3600000LL;
        /* If wall clock is available, use it instead */
        time_t now = time(nullptr);
        if (now > 1000000) {
            expires_ms = ((int64_t)now + 3600) * 1000LL;
        }
        LOG_I("claude_auth", "set_tokens: expires=0, assuming +1h");
    } else if (expires_unix > 100000000000LL) {
        /* Already milliseconds */
        expires_ms = expires_unix;
        LOG_I("claude_auth", "set_tokens: expires_unix interpreted as ms");
    } else {
        /* Seconds → milliseconds */
        expires_ms = expires_unix * 1000LL;
        LOG_I("claude_auth", "set_tokens: expires_unix=%lld sec → ms", (long long)expires_unix);
    }

    put_i64(ns, "exphi", "explo", expires_ms);
    LOG_I("claude_auth", "set_tokens: index %u tokens updated", idx);
    return true;
}

bool set_active(uint8_t idx) {
    if (idx >= profile_count()) return false;
    nvs_store::put_u8(NS_GLOBAL, KEY_ACTIVE, idx);
    LOG_I("claude_auth", "set_active: active → %u", idx);
    return true;
}

String get_active_access_token(bool* is_expired_out) {
    if (is_expired_out) *is_expired_out = false;
    uint8_t count = profile_count();
    if (count == 0) return "";
    uint8_t idx = active_index();
    if (idx >= count) return "";

    char ns[8];
    profile_ns(idx, ns, sizeof(ns));
    String acc = nvs_store::get_str(ns, "access", "");
    if (acc.isEmpty()) return "";

    /* Check expiry */
    int64_t exp_ms = get_i64(ns, "exphi", "explo");
    if (exp_ms > 0) {
        int64_t now_ms = (int64_t)time(nullptr) * 1000LL;
        /* Only check if wall clock looks valid */
        if (now_ms > 1000000000000LL && now_ms > exp_ms + 60000LL) {
            /* Expired more than 1 minute ago */
            if (is_expired_out) *is_expired_out = true;
            LOG_W("claude_auth", "access token for profile %u is expired", idx);
            /* Still return the token — let caller decide (v1: we return it anyway,
               caller shows "Token expired" badge but still attempts the request) */
        }
    }
    return acc;
}

String get_active_label() {
    uint8_t count = profile_count();
    if (count == 0) return "(none)";
    uint8_t idx = active_index();
    if (idx >= count) return "(none)";
    char ns[8];
    profile_ns(idx, ns, sizeof(ns));
    return nvs_store::get_str(ns, "label", "(unnamed)");
}

bool is_token_expired() {
    bool expired = false;
    get_active_access_token(&expired);
    return expired;
}

/** True once SNTP has produced a plausible wall clock (post-2001). */
bool wall_clock_ready() {
    return (int64_t)time(nullptr) > 1000000000LL;
}

bool refresh_active() {
    uint8_t count = profile_count();
    if (count == 0) {
        LOG_W("claude_auth", "refresh_active: no profiles");
        return false;
    }
    uint8_t idx = active_index();
    if (idx >= count) {
        LOG_W("claude_auth", "refresh_active: active index out of range");
        return false;
    }

    char ns[8];
    profile_ns(idx, ns, sizeof(ns));
    String rt = nvs_store::get_str(ns, "refresh", "");
    if (rt.isEmpty()) {
        LOG_W("claude_auth", "refresh_active: no refresh token stored");
        return false;
    }

    static const char* TOKEN_URL  = "https://platform.claude.com/v1/oauth/token";
    static const char* CLIENT_ID  = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
    static const char* SCOPE      = "user:profile user:inference user:sessions:claude_code user:mcp_servers";

    /* Build JSON body */
    JsonDocument req;
    req["grant_type"]    = "refresh_token";
    req["refresh_token"] = rt;
    req["client_id"]     = CLIENT_ID;
    req["scope"]         = SCOPE;
    String body;
    serializeJson(req, body);

    LOG_I("claude_auth", "refresh_active: POST %s", TOKEN_URL);

    WiFiClientSecure client;
    client.setInsecure();  // TODO: pin Anthropic CA
    HTTPClient http;
    http.begin(client, TOKEN_URL);
    http.setTimeout(10000);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(body);

    if (code != 200) {
        String errBody = http.getString();
        http.end();
        client.stop();
        LOG_W("claude_auth", "refresh_active: HTTP %d  body=%s",
              code, errBody.substring(0, 80).c_str());
        return false;
    }

    String resp = http.getString();
    http.end();
    client.stop();

    JsonDocument res;
    DeserializationError err = deserializeJson(res, resp);
    if (err) {
        LOG_W("claude_auth", "refresh_active: JSON parse error: %s", err.c_str());
        return false;
    }

    const char* new_access  = res["access_token"]  | (const char*)nullptr;
    const char* new_refresh = res["refresh_token"] | (const char*)nullptr;
    int64_t     expires_in  = res["expires_in"]    | (int64_t)0;

    if (!new_access || strlen(new_access) < 10) {
        LOG_W("claude_auth", "refresh_active: response missing access_token");
        return false;
    }

    /* If refresh token is not rotated, keep the old one */
    const char* rt_to_store = (new_refresh && strlen(new_refresh) > 10)
                              ? new_refresh
                              : rt.c_str();

    /* expires_in is in seconds; convert to absolute Unix ms */
    int64_t expires_ms = 0;
    if (expires_in > 0) {
        time_t now = time(nullptr);
        if (now > 1000000) {
            expires_ms = ((int64_t)now + expires_in) * 1000LL;
        } else {
            /* No wall clock — estimate from millis() */
            expires_ms = (int64_t)millis() + expires_in * 1000LL;
        }
    }

    nvs_store::put_str(ns, "access", new_access);
    nvs_store::put_str(ns, "refresh", rt_to_store);
    if (expires_ms > 0) {
        put_i64(ns, "exphi", "explo", expires_ms);
    }

    LOG_I("claude_auth", "refresh_active: OK — new token expires_in=%lld s",
          (long long)expires_in);
    return true;
}

bool set_tokens_by_label(const char* label,
                          const char* access,
                          const char* refresh_tok,
                          int64_t     expires_unix) {
    uint8_t idx = find_by_label(label);
    if (idx == 255) {
        LOG_W("claude_auth", "set_tokens_by_label: label '%s' not found", label);
        return false;
    }
    return set_tokens(idx, access, refresh_tok, expires_unix);
}

} // namespace claude_auth

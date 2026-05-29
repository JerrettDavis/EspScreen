#pragma once
#include <Arduino.h>
#include <stdint.h>

/**
 * claude_auth — Multi-profile OAuth token storage for Claude.
 *
 * NVS layout:
 *   Namespace "claude"       → count (u8), active (u8)
 *   Namespace "cl_p<N>"     → label (str), access (str), refresh (str), expires_at (i64 via two i32)
 *
 * Max 4 profiles.  Provisioned via serial commands (see main.cpp) or WiFi push
 * via the HTTP API server (api_server.h).
 *
 * Token refresh: implemented via Anthropic's OAuth token endpoint.
 *   URL:     https://platform.claude.com/v1/oauth/token
 *   Method:  POST application/json
 *   Body:    { grant_type: "refresh_token", refresh_token: "<rt>",
 *              client_id: "9d1c250a-e61b-44d9-88ed-5944d1962f5e",
 *              scope: "user:profile user:inference user:sessions:claude_code user:mcp_servers" }
 *   Returns: { access_token, refresh_token (if rotated), expires_in (seconds) }
 *
 * TODO: pin Anthropic CA cert (currently using setInsecure()).
 */

namespace claude_auth {

static constexpr uint8_t MAX_PROFILES = 4;

struct Profile {
    uint8_t  index;
    char     label[32];     // human-readable, ≤ 30 chars
    char     access[512];   // OAuth access token (long)
    char     refresh[256];  // OAuth refresh token
    int64_t  expires_at_ms; // Unix milliseconds; 0 = unknown
};

/** Load all profile metadata on boot (migration + sanity check). */
void init();

/** Return the number of stored profiles (0..MAX_PROFILES). */
uint8_t profile_count();

/** Return index of the active profile (0..count-1), or 255 if none. */
uint8_t active_index();

/** Load a profile by index.  Returns false if index is out of range. */
bool load_profile(uint8_t idx, Profile& out);

/** Find a profile by label (case-insensitive).  Returns 255 if not found. */
uint8_t find_by_label(const char* label);

/**
 * Add a new profile with the given label.
 * Returns the index (0..3) or 255 on failure (max profiles reached or label exists).
 * Automatically sets active to this profile if it is the first one.
 */
uint8_t add_profile(const char* label);

/** Remove a profile by index.  Re-indexes remaining profiles. */
bool remove_profile(uint8_t idx);

/**
 * Set OAuth tokens for a profile by index.
 * expires_unix_sec: Unix timestamp in seconds when the access token expires.
 *   Pass 0 to mean "unknown — assume 1 hour from now."
 *   Auto-detects if value is in ms vs sec: values > 1e11 are treated as ms.
 */
bool set_tokens(uint8_t idx, const char* access, const char* refresh, int64_t expires_unix_sec);

/** Switch the active profile to the given index. */
bool set_active(uint8_t idx);

/**
 * Get the access token for the active profile.
 * Returns empty string if no profile, or if token is expired.
 * Writes true to *is_expired if the token exists but is past its expiry.
 */
String get_active_access_token(bool* is_expired = nullptr);

/** Get the label of the active profile, or "(none)" if no profiles. */
String get_active_label();

/** Check whether the active token appears to be expired. */
bool is_token_expired();

/**
 * Refresh the active profile's tokens against Anthropic's OAuth endpoint.
 * Requires WiFi to be connected.  Blocks until the HTTP round-trip completes
 * or times out (10 s).
 *
 * On success: NVS is updated with the new access token, (possibly rotated)
 *             refresh token, and new expiry.  Returns true.
 * On failure: NVS is left unchanged.  Returns false.
 *             Reason is logged via LOG_W.
 */
bool refresh_active();

/**
 * Set tokens by label string (convenience for the HTTP API server).
 * Finds profile by label, then calls set_tokens().
 * Returns false if label not found or set_tokens fails.
 */
bool set_tokens_by_label(const char* label,
                          const char* access,
                          const char* refresh_tok,
                          int64_t     expires_unix);

} // namespace claude_auth

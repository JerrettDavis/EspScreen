/**
 * refresh-lib.mjs — Pure, testable helpers for Claude OAuth token refresh.
 *
 * Zero side effects: no fs, no network, no Date.now() calls.
 * All time-sensitive functions accept `nowMs` as an injected parameter.
 *
 * Run tests: node --test tools/creds-watcher/test_refresh.mjs
 */

// ─── Constants ────────────────────────────────────────────────────────────────

export const TOKEN_URL = 'https://platform.claude.com/v1/oauth/token';

export const CLIENT_ID = '9d1c250a-e61b-44d9-88ed-5944d1962f5e';

export const SCOPE = 'user:profile user:inference user:sessions:claude_code user:mcp_servers';

// ─── needsRefresh ─────────────────────────────────────────────────────────────

/**
 * Determine whether a token should be refreshed.
 *
 * Returns true when:
 *   - expiresAtMs is missing, falsy, or zero, OR
 *   - the token expires within `skewMs` of `nowMs`
 *     (i.e. expiresAtMs - nowMs <= skewMs)
 *
 * @param {number|undefined|null} expiresAtMs  Token expiry in Unix milliseconds.
 * @param {number}                nowMs         Current time in Unix milliseconds.
 * @param {number}                skewMs        Proactive-refresh window in ms.
 * @returns {boolean}
 */
export function needsRefresh(expiresAtMs, nowMs, skewMs) {
  if (!expiresAtMs) return true;
  return (expiresAtMs - nowMs) <= skewMs;
}

// ─── buildRefreshBody ─────────────────────────────────────────────────────────

/**
 * Build the JSON body for a refresh_token grant request.
 *
 * @param {string} refreshToken  The current refresh token.
 * @returns {object}             Plain object ready for JSON.stringify.
 */
export function buildRefreshBody(refreshToken) {
  return {
    grant_type:    'refresh_token',
    refresh_token: refreshToken,
    client_id:     CLIENT_ID,
    scope:         SCOPE,
  };
}

// ─── isValidTokenResponse ─────────────────────────────────────────────────────

/**
 * Validate an OAuth token response object.
 *
 * Returns true only when:
 *   - resp is a non-null object
 *   - resp.access_token is a non-empty string
 *   - resp.expires_in is a strictly positive finite number
 *
 * @param {unknown} resp
 * @returns {boolean}
 */
export function isValidTokenResponse(resp) {
  if (!resp || typeof resp !== 'object') return false;
  if (typeof resp.access_token !== 'string' || resp.access_token.length === 0) return false;
  if (typeof resp.expires_in !== 'number' || resp.expires_in <= 0 || !isFinite(resp.expires_in)) return false;
  return true;
}

// ─── mergeRefreshed ───────────────────────────────────────────────────────────

/**
 * Produce a new claudeAiOauth object from a validated token response.
 *
 * Refresh-token rotation: if resp.refresh_token is present and non-empty the
 * new value is used; otherwise the original token is preserved (non-rotating
 * servers are valid per the OAuth spec).
 *
 * Does NOT mutate oldOauth.
 *
 * @param {object} oldOauth  Existing claudeAiOauth object from credentials.
 * @param {object} resp      Validated token response from the OAuth server.
 * @param {number} nowMs     Current time in Unix milliseconds (injected).
 * @returns {object}         New claudeAiOauth object.
 */
export function mergeRefreshed(oldOauth, resp, nowMs) {
  const refreshToken =
    (resp.refresh_token && resp.refresh_token.length > 0)
      ? resp.refresh_token
      : oldOauth.refreshToken;

  return {
    // Spread any extra fields that may exist inside oldOauth (future-proofing)
    ...oldOauth,
    accessToken:  resp.access_token,
    refreshToken,
    expiresAt:    nowMs + resp.expires_in * 1000,
  };
}

// ─── redactToken ──────────────────────────────────────────────────────────────

/**
 * Produce a safe display string for a token — never reveals the full value.
 *
 * For tokens longer than 8 chars returns "first4…last4".
 * For short, empty, or null tokens returns "(none)".
 *
 * @param {string|null|undefined} tok
 * @returns {string}
 */
export function redactToken(tok) {
  if (!tok || typeof tok !== 'string' || tok.length <= 8) return '(none)';
  return `${tok.slice(0, 4)}…${tok.slice(-4)}`;
}

// ─── toExpiresAtSeconds ───────────────────────────────────────────────────────

/**
 * Convert a millisecond Unix timestamp to whole seconds (for the device payload).
 *
 * @param {number} expiresAtMs
 * @returns {number}
 */
export function toExpiresAtSeconds(expiresAtMs) {
  return Math.floor(expiresAtMs / 1000);
}

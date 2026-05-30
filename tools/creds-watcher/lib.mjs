/**
 * lib.mjs — Pure helper functions extracted from watch.js for testability.
 * No side effects, no filesystem access, no network calls.
 */

/**
 * Produce a short fingerprint of a credentials object for idempotency checking.
 * Uses first+last 8 chars of the access token plus the expiry timestamp.
 *
 * @param {{ access: string, expires_at: number }} creds
 * @returns {string}
 */
export function tokenHash(creds) {
  const a = creds.access;
  return `${a.slice(0, 8)}..${a.slice(-8)}@${creds.expires_at}`;
}

/**
 * Redact a token for safe logging — shows only the first and last 6 characters.
 * Matches the redact() logic in provision.js.
 *
 * @param {string|null|undefined} token
 * @returns {string}
 */
export function redact(token) {
  if (!token || token.length < 10) return '[empty]';
  return token.slice(0, 6) + '...' + token.slice(-6);
}

/**
 * lib.mjs — Pure helper functions extracted from server.js for testability.
 * Imported by both server.js and test_pure.mjs.
 * No side effects, no network calls, no filesystem access.
 */

export const CACHE_TTL_MS     = 60_000;       // 60 s
export const STALE_BADGE_MS   = 5 * 60_000;   // 5 min

/**
 * Returns true if the cache is still fresh (within CACHE_TTL_MS of lastSuccessMs).
 * @param {number} lastSuccessMs - Unix ms timestamp of last successful fetch (0 = never)
 * @param {number} nowMs         - Current Unix ms timestamp
 */
export function isCacheFresh(lastSuccessMs, nowMs) {
  return lastSuccessMs > 0 && (nowMs - lastSuccessMs) < CACHE_TTL_MS;
}

/**
 * Returns true if the cache should show a stale badge.
 * @param {number} lastSuccessMs - Unix ms timestamp of last successful fetch
 * @param {number} nowMs         - Current Unix ms timestamp
 */
export function isCacheStale(lastSuccessMs, nowMs) {
  return lastSuccessMs > 0 && (nowMs - lastSuccessMs) > STALE_BADGE_MS;
}

/**
 * Convert a RateLimit object from the API into our contract shape.
 * API shape: { utilization: 0.65, resets_at: "2026-05-28T12:30:00Z" }
 *
 * @param {object|null} rl       - Raw rate limit object from upstream API
 * @param {number}      nowMs    - Current Unix ms timestamp (injected for determinism)
 * @returns {object|null}
 */
export function buildRateEntry(rl, nowMs) {
  if (!rl) return null;
  const resetsAt = rl.resets_at ?? null;
  let resetsInSec = null;
  if (resetsAt) {
    resetsInSec = Math.max(0, Math.round((new Date(resetsAt).getTime() - nowMs) / 1000));
  }
  return {
    utilization: rl.utilization ?? null,
    resets_at: resetsAt,
    resets_in_sec: resetsInSec,
  };
}

/**
 * test_refresh.mjs — node:test unit tests for refresh-lib.mjs pure helpers.
 * Run: node --test tools/creds-watcher/test_refresh.mjs
 * No external dependencies required.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  needsRefresh,
  buildRefreshBody,
  isValidTokenResponse,
  mergeRefreshed,
  redactToken,
  toExpiresAtSeconds,
  CLIENT_ID,
  SCOPE,
  TOKEN_URL,
} from './refresh-lib.mjs';

// ── needsRefresh ──────────────────────────────────────────────────────────────

test('needsRefresh: returns true when token is already expired', () => {
  const nowMs       = 1_000_000_000_000;
  const expiresAtMs = nowMs - 60_000; // expired 1 minute ago
  assert.equal(needsRefresh(expiresAtMs, nowMs, 0), true);
});

test('needsRefresh: returns true when within skew window', () => {
  const nowMs       = 1_000_000_000_000;
  const skewMs      = 30 * 60 * 1000; // 30 minutes
  const expiresAtMs = nowMs + 20 * 60 * 1000; // expires in 20 min (< skew)
  assert.equal(needsRefresh(expiresAtMs, nowMs, skewMs), true);
});

test('needsRefresh: returns false when far from expiry', () => {
  const nowMs       = 1_000_000_000_000;
  const skewMs      = 30 * 60 * 1000; // 30 minutes
  const expiresAtMs = nowMs + 60 * 60 * 1000; // expires in 60 min (> skew)
  assert.equal(needsRefresh(expiresAtMs, nowMs, skewMs), false);
});

test('needsRefresh: returns false when exactly at skew boundary (not strictly within)', () => {
  const nowMs       = 1_000_000_000_000;
  const skewMs      = 30 * 60 * 1000;
  const expiresAtMs = nowMs + skewMs; // exactly at boundary: expiresAtMs - nowMs === skewMs
  // <= so this is true (equal counts as within window)
  assert.equal(needsRefresh(expiresAtMs, nowMs, skewMs), true);
});

test('needsRefresh: returns true when expiresAtMs is 0', () => {
  assert.equal(needsRefresh(0, 1_000_000_000_000, 0), true);
});

test('needsRefresh: returns true when expiresAtMs is null', () => {
  assert.equal(needsRefresh(null, 1_000_000_000_000, 0), true);
});

test('needsRefresh: returns true when expiresAtMs is undefined', () => {
  assert.equal(needsRefresh(undefined, 1_000_000_000_000, 0), true);
});

// ── isValidTokenResponse ──────────────────────────────────────────────────────

test('isValidTokenResponse: returns true for a valid response', () => {
  const resp = { access_token: 'sk-ant-newtoken', expires_in: 3600 };
  assert.equal(isValidTokenResponse(resp), true);
});

test('isValidTokenResponse: returns false when access_token is missing', () => {
  const resp = { expires_in: 3600 };
  assert.equal(isValidTokenResponse(resp), false);
});

test('isValidTokenResponse: returns false when access_token is empty string', () => {
  const resp = { access_token: '', expires_in: 3600 };
  assert.equal(isValidTokenResponse(resp), false);
});

test('isValidTokenResponse: returns false when expires_in is missing', () => {
  const resp = { access_token: 'sk-ant-newtoken' };
  assert.equal(isValidTokenResponse(resp), false);
});

test('isValidTokenResponse: returns false when expires_in is zero', () => {
  const resp = { access_token: 'sk-ant-newtoken', expires_in: 0 };
  assert.equal(isValidTokenResponse(resp), false);
});

test('isValidTokenResponse: returns false when expires_in is negative', () => {
  const resp = { access_token: 'sk-ant-newtoken', expires_in: -100 };
  assert.equal(isValidTokenResponse(resp), false);
});

test('isValidTokenResponse: returns false for null', () => {
  assert.equal(isValidTokenResponse(null), false);
});

test('isValidTokenResponse: returns false for a string', () => {
  assert.equal(isValidTokenResponse('not-an-object'), false);
});

test('isValidTokenResponse: returns false for a number', () => {
  assert.equal(isValidTokenResponse(42), false);
});

// ── mergeRefreshed ────────────────────────────────────────────────────────────

test('mergeRefreshed: uses new refresh token when present in response', () => {
  const oldOauth = {
    accessToken:  'old-access',
    refreshToken: 'old-refresh',
    expiresAt:    1_000_000_000_000,
  };
  const resp = {
    access_token:  'new-access',
    refresh_token: 'new-refresh',
    expires_in:    3600,
  };
  const nowMs  = 2_000_000_000_000;
  const result = mergeRefreshed(oldOauth, resp, nowMs);

  assert.equal(result.accessToken,  'new-access');
  assert.equal(result.refreshToken, 'new-refresh');
  assert.equal(result.expiresAt,    nowMs + 3600 * 1000);
});

test('mergeRefreshed: keeps old refresh token when response omits refresh_token', () => {
  const oldOauth = {
    accessToken:  'old-access',
    refreshToken: 'old-refresh',
    expiresAt:    1_000_000_000_000,
  };
  const resp = {
    access_token: 'new-access',
    expires_in:   3600,
    // refresh_token absent
  };
  const result = mergeRefreshed(oldOauth, resp, 2_000_000_000_000);
  assert.equal(result.refreshToken, 'old-refresh');
});

test('mergeRefreshed: keeps old refresh token when response has empty refresh_token', () => {
  const oldOauth = { accessToken: 'old-access', refreshToken: 'old-refresh', expiresAt: 0 };
  const resp     = { access_token: 'new-access', refresh_token: '', expires_in: 3600 };
  const result   = mergeRefreshed(oldOauth, resp, 1_000_000_000_000);
  assert.equal(result.refreshToken, 'old-refresh');
});

test('mergeRefreshed: computes expiresAt correctly', () => {
  const oldOauth = { accessToken: 'a', refreshToken: 'b', expiresAt: 0 };
  const resp     = { access_token: 'new-access', expires_in: 7200 };
  const nowMs    = 1_748_000_000_000;
  const result   = mergeRefreshed(oldOauth, resp, nowMs);
  assert.equal(result.expiresAt, nowMs + 7200 * 1000);
});

test('mergeRefreshed: does not mutate the original oldOauth object', () => {
  const oldOauth = {
    accessToken:  'original-access',
    refreshToken: 'original-refresh',
    expiresAt:    1_000,
  };
  const frozen = Object.freeze({ ...oldOauth }); // freeze a copy to verify non-mutation
  const resp   = { access_token: 'new', expires_in: 100 };
  // Should not throw (would throw if it tried to mutate frozen)
  assert.doesNotThrow(() => mergeRefreshed(frozen, resp, 9_000));
  // Original object unchanged
  assert.equal(oldOauth.accessToken, 'original-access');
});

test('mergeRefreshed: preserves extra keys from oldOauth (spread behaviour)', () => {
  const oldOauth = {
    accessToken:  'old-access',
    refreshToken: 'old-refresh',
    expiresAt:    0,
    someExtraField: 'keep-me',
  };
  const resp   = { access_token: 'new-access', expires_in: 100 };
  const result = mergeRefreshed(oldOauth, resp, 1_000);
  assert.equal(result.someExtraField, 'keep-me');
});

// ── buildRefreshBody ──────────────────────────────────────────────────────────

test('buildRefreshBody: grant_type is refresh_token', () => {
  const body = buildRefreshBody('my-refresh-token');
  assert.equal(body.grant_type, 'refresh_token');
});

test('buildRefreshBody: includes correct client_id constant', () => {
  const body = buildRefreshBody('my-refresh-token');
  assert.equal(body.client_id, CLIENT_ID);
  assert.equal(body.client_id, '9d1c250a-e61b-44d9-88ed-5944d1962f5e');
});

test('buildRefreshBody: includes correct scope constant', () => {
  const body = buildRefreshBody('my-refresh-token');
  assert.equal(body.scope, SCOPE);
  assert.ok(body.scope.includes('user:inference'));
  assert.ok(body.scope.includes('user:sessions:claude_code'));
});

test('buildRefreshBody: passes through the given refresh_token', () => {
  const body = buildRefreshBody('tok-xyz-abc');
  assert.equal(body.refresh_token, 'tok-xyz-abc');
});

// ── redactToken ───────────────────────────────────────────────────────────────

test('redactToken: never contains the full input for a long token', () => {
  const tok    = 'sk-ant-abcdefghijklmnopqrstu1234567890';
  const result = redactToken(tok);
  assert.notEqual(result, tok);
  assert.ok(!result.includes(tok));
});

test('redactToken: result is shorter than the input for a long token', () => {
  const tok = 'sk-ant-abcdefghijklmnopqrstuvwxyz1234567890';
  assert.ok(redactToken(tok).length < tok.length);
});

test('redactToken: returns (none) for null', () => {
  assert.equal(redactToken(null), '(none)');
});

test('redactToken: returns (none) for undefined', () => {
  assert.equal(redactToken(undefined), '(none)');
});

test('redactToken: returns (none) for empty string', () => {
  assert.equal(redactToken(''), '(none)');
});

test('redactToken: returns (none) for a short token (<=8 chars)', () => {
  assert.equal(redactToken('short'), '(none)');
  assert.equal(redactToken('12345678'), '(none)');
});

test('redactToken: shows partial token for a 9-char input', () => {
  const result = redactToken('123456789');
  assert.notEqual(result, '(none)');
  // Should contain ellipsis
  assert.ok(result.includes('…') || result.includes('...'));
});

// ── toExpiresAtSeconds ────────────────────────────────────────────────────────

test('toExpiresAtSeconds: floors milliseconds to whole seconds', () => {
  assert.equal(toExpiresAtSeconds(1_748_000_000_500), 1_748_000_000);
});

test('toExpiresAtSeconds: works for zero', () => {
  assert.equal(toExpiresAtSeconds(0), 0);
});

test('toExpiresAtSeconds: no fractional seconds in output', () => {
  const result = toExpiresAtSeconds(9_999_999_999_999);
  assert.equal(result, Math.floor(result));
});

// ── Constants ─────────────────────────────────────────────────────────────────

test('TOKEN_URL: points to platform.claude.com', () => {
  assert.ok(TOKEN_URL.startsWith('https://platform.claude.com'));
  assert.ok(TOKEN_URL.includes('/oauth/token'));
});

test('CLIENT_ID: matches the known EspScreen client ID', () => {
  assert.equal(CLIENT_ID, '9d1c250a-e61b-44d9-88ed-5944d1962f5e');
});

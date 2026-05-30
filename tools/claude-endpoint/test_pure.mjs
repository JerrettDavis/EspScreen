/**
 * test_pure.mjs — node:test unit tests for claude-endpoint pure helpers.
 * Run: node --test tools/claude-endpoint/test_pure.mjs
 * No external dependencies required.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  CACHE_TTL_MS,
  STALE_BADGE_MS,
  isCacheFresh,
  isCacheStale,
  buildRateEntry,
} from './lib.mjs';

// ── isCacheFresh ──────────────────────────────────────────────────────────────

test('isCacheFresh: returns false when lastSuccessMs is 0 (never fetched)', () => {
  assert.equal(isCacheFresh(0, Date.now()), false);
});

test('isCacheFresh: returns true when fetched less than TTL ago', () => {
  const now = 1_000_000_000;
  const last = now - CACHE_TTL_MS + 1;  // 1 ms before expiry
  assert.equal(isCacheFresh(last, now), true);
});

test('isCacheFresh: returns false when fetched more than TTL ago', () => {
  const now = 1_000_000_000;
  const last = now - CACHE_TTL_MS - 1;  // 1 ms past expiry
  assert.equal(isCacheFresh(last, now), false);
});

// ── isCacheStale ──────────────────────────────────────────────────────────────

test('isCacheStale: returns false when lastSuccessMs is 0 (never fetched)', () => {
  assert.equal(isCacheStale(0, Date.now()), false);
});

test('isCacheStale: returns false when within stale window', () => {
  const now = 1_000_000_000;
  const last = now - STALE_BADGE_MS + 1;
  assert.equal(isCacheStale(last, now), false);
});

test('isCacheStale: returns true when beyond stale window', () => {
  const now = 1_000_000_000;
  const last = now - STALE_BADGE_MS - 1;
  assert.equal(isCacheStale(last, now), true);
});

// ── buildRateEntry ────────────────────────────────────────────────────────────

test('buildRateEntry: returns null for null input', () => {
  assert.equal(buildRateEntry(null, Date.now()), null);
});

test('buildRateEntry: passthrough for null resets_at', () => {
  const entry = buildRateEntry({ utilization: 0.42, resets_at: null }, 1_000_000_000_000);
  assert.equal(entry.utilization, 0.42);
  assert.equal(entry.resets_at, null);
  assert.equal(entry.resets_in_sec, null);
});

test('buildRateEntry: computes resets_in_sec from future resets_at', () => {
  // resets_at is exactly 3600 seconds in the future
  const nowMs = 1_748_000_000_000;
  const futureMs = nowMs + 3600 * 1000;
  const resetsAt = new Date(futureMs).toISOString();
  const entry = buildRateEntry({ utilization: 0.65, resets_at: resetsAt }, nowMs);
  assert.equal(entry.resets_in_sec, 3600);
  assert.equal(entry.utilization, 0.65);
});

test('buildRateEntry: clamps negative resets_in_sec to 0 for past timestamps', () => {
  const nowMs = 1_748_000_000_000;
  const pastMs = nowMs - 5000;
  const resetsAt = new Date(pastMs).toISOString();
  const entry = buildRateEntry({ utilization: 1.0, resets_at: resetsAt }, nowMs);
  assert.equal(entry.resets_in_sec, 0);
});

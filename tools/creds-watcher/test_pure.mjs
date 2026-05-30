/**
 * test_pure.mjs — node:test unit tests for creds-watcher pure helpers.
 * Run: node --test tools/creds-watcher/test_pure.mjs
 * No external dependencies required.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';

import { tokenHash, redact } from './lib.mjs';

// ── tokenHash ─────────────────────────────────────────────────────────────────

test('tokenHash: produces expected fingerprint shape', () => {
  const creds = { access: 'abcdefgh12345678', expires_at: 1_748_000_000_000 };
  const hash = tokenHash(creds);
  assert.equal(hash, 'abcdefgh..12345678@1748000000000');
});

test('tokenHash: different tokens produce different hashes', () => {
  const a = { access: 'AAAAAAAABBBBBBBB', expires_at: 1_000 };
  const b = { access: 'AAAAAAAACCCCCCCC', expires_at: 1_000 };
  assert.notEqual(tokenHash(a), tokenHash(b));
});

test('tokenHash: same token + same expiry → same hash (idempotency)', () => {
  const creds = { access: 'tok1234567890xyz', expires_at: 9999 };
  assert.equal(tokenHash(creds), tokenHash(creds));
});

// ── redact ────────────────────────────────────────────────────────────────────

test('redact: shows first+last 6 chars for long token', () => {
  const result = redact('sk-ant-abcdef123456ghijkl');
  assert.match(result, /^sk-ant\.\.\./);
  assert.match(result, /jkl$/);
});

test('redact: returns [empty] for null', () => {
  assert.equal(redact(null), '[empty]');
});

test('redact: returns [empty] for short token (<10 chars)', () => {
  assert.equal(redact('short'), '[empty]');
});

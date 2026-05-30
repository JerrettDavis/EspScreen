#!/usr/bin/env node
/**
 * refresh.js — Host-side Claude OAuth token refresh tool for EspScreen.
 *
 * Reads ~/.claude/.credentials.json (or CLAUDE_CONFIG_DIR if set), performs an
 * OAuth refresh_token grant to renew short-lived access tokens, writes the
 * result back safely (atomic rename + .bak), then pushes the fresh tokens to
 * the ESP32 device via the shared push.js mechanism.
 *
 * Usage:
 *   node refresh.js [options]
 *
 * Options:
 *   --force                   Refresh even if the token is not near expiry
 *   --no-push                 Refresh credentials file only; skip device push
 *   --label <name>            Profile label passed to pushTokens (default: "Default")
 *   --threshold-min <N>       Minutes before expiry to proactively refresh (default: 30)
 *   --debug                   Verbose logging
 *   -h, --help                Show this help text
 *
 * Exit codes:
 *   0  Success (refreshed and/or pushed, or skipped because not near expiry)
 *   2  Credentials file not found
 *   3  No refresh token in credentials (run `claude login`)
 *   4  Token refresh failed — credentials file left UNTOUCHED
 */

import { readFileSync, writeFileSync, copyFileSync, renameSync, existsSync, mkdirSync, statSync } from 'node:fs';
import { join, dirname } from 'node:path';
import os from 'node:os';
import { parseArgs } from 'node:util';

import {
  TOKEN_URL,
  needsRefresh,
  buildRefreshBody,
  isValidTokenResponse,
  mergeRefreshed,
  redactToken,
  toExpiresAtSeconds,
} from './refresh-lib.mjs';

// ─── CLI args ─────────────────────────────────────────────────────────────────

const { values: args } = parseArgs({
  options: {
    force:            { type: 'boolean', default: false },
    'no-push':        { type: 'boolean', default: false },
    label:            { type: 'string',  default: 'Default' },
    'threshold-min':  { type: 'string',  default: '30' },
    debug:            { type: 'boolean', default: false },
    help:             { type: 'boolean', default: false, short: 'h' },
  },
  allowPositionals: false,
});

if (args.help) {
  console.log(`
espscreen refresh — renew Claude OAuth tokens and push to ESP32

Usage:
  node refresh.js [options]

Options:
  --force                  Refresh even if token is not near expiry
  --no-push                Skip device push after refreshing
  --label <name>           Profile label for device push (default: "Default")
  --threshold-min <N>      Minutes before expiry to proactively refresh (default: 30)
  --debug                  Verbose logging
  -h, --help               Show this help

Credentials file location:
  Default:  ~/.claude/.credentials.json
  Override: set CLAUDE_CONFIG_DIR env var (replaces the .claude directory)

Exit codes:
  0  Success (refreshed / pushed / or skipped — not near expiry)
  2  Credentials file not found
  3  No refresh token — run \`claude login\`
  4  Token refresh failed (file left untouched)

Safe-write guarantee:
  Before any write, a .bak copy is made.  The new JSON is written to a .tmp
  file then atomically renamed over the real path.  If anything fails partway
  through, the original file is untouched and .bak provides a recovery copy.
`);
  process.exit(0);
}

const noPush      = args['no-push'];
const label       = args.label;
const thresholdMs = parseInt(args['threshold-min'], 10) * 60 * 1000;
const isForce     = args.force;
const isDebug     = args.debug;

// ─── Logging ──────────────────────────────────────────────────────────────────

function ts() {
  return new Date().toISOString();
}

function log(msg, ...extra) {
  console.log(`[${ts()}] ${msg}`, ...extra);
}

function dbg(msg, ...extra) {
  if (isDebug) log(`[dbg] ${msg}`, ...extra);
}

function warn(msg, ...extra) {
  console.warn(`[${ts()}] WARN ${msg}`, ...extra);
}

// ─── Credentials path ─────────────────────────────────────────────────────────

function resolveCredsPath() {
  if (process.env.CLAUDE_CONFIG_DIR) {
    return join(process.env.CLAUDE_CONFIG_DIR, '.credentials.json');
  }
  return join(os.homedir(), '.claude', '.credentials.json');
}

// ─── Main ─────────────────────────────────────────────────────────────────────

async function main() {
  const credsPath = resolveCredsPath();
  const bakPath   = credsPath + '.bak';
  const tmpPath   = credsPath + '.tmp';

  dbg(`Credentials path: ${credsPath}`);
  dbg(`Threshold: ${thresholdMs / 60000} minutes`);

  // ── 1. Read credentials file ───────────────────────────────────────────────

  if (!existsSync(credsPath)) {
    log(`Error: credentials file not found: ${credsPath}`);
    log(`Run \`claude login\` to create credentials, or set CLAUDE_CONFIG_DIR.`);
    process.exit(2);
  }

  let rawJson;
  let fullCreds;
  try {
    rawJson    = readFileSync(credsPath, 'utf8');
    fullCreds  = JSON.parse(rawJson);
  } catch (err) {
    log(`Error: cannot read/parse credentials file: ${err.message}`);
    process.exit(2);
  }

  const oldOauth = fullCreds.claudeAiOauth;
  if (!oldOauth || !oldOauth.refreshToken) {
    log(`Error: no refresh token found in credentials.`);
    log(`Run \`claude login\` to obtain fresh OAuth credentials.`);
    process.exit(3);
  }

  const { accessToken, refreshToken, expiresAt } = oldOauth;
  dbg(`Current access token:  ${redactToken(accessToken)}`);
  dbg(`Current refresh token: ${redactToken(refreshToken)}`);
  dbg(`Current expiresAt:     ${expiresAt ? new Date(expiresAt).toISOString() : '(unset)'}`);

  const nowMs     = Date.now();
  const shouldRefresh = isForce || needsRefresh(expiresAt, nowMs, thresholdMs);

  if (!shouldRefresh) {
    const minutesLeft = Math.round((expiresAt - nowMs) / 60000);
    log(`Token still valid (expires in ~${minutesLeft}m), skipping refresh.`);

    // Even if skipping refresh, still push existing tokens to keep device in sync.
    if (!noPush) {
      await doPush({ access: accessToken, refresh: refreshToken, expiresAt, label });
    }

    log(`Summary: no-refresh | ${noPush ? 'no-push' : 'push-attempted'} | expires ${new Date(expiresAt).toLocaleString()}`);
    process.exit(0);
  }

  // ── 2. Perform the OAuth refresh grant ────────────────────────────────────

  log(`Refreshing token (force=${isForce})…`);
  dbg(`POST ${TOKEN_URL}`);

  const body    = buildRefreshBody(refreshToken);
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 20_000);

  let respObj;
  try {
    const resp = await fetch(TOKEN_URL, {
      method:  'POST',
      headers: { 'Content-Type': 'application/json' },
      body:    JSON.stringify(body),
      signal:  controller.signal,
    });

    clearTimeout(timeout);

    if (!resp.ok) {
      const text = await resp.text().catch(() => '');
      log(`Error: OAuth server returned HTTP ${resp.status}.`);
      dbg(`Response body (truncated): ${text.slice(0, 300)}`);
      log(`Credentials file NOT modified.`);
      process.exit(4);
    }

    respObj = await resp.json();
  } catch (err) {
    clearTimeout(timeout);
    const reason = err.name === 'AbortError' ? 'request timed out (20s)' : err.message;
    log(`Error: token refresh network error — ${reason}`);
    log(`Credentials file NOT modified.`);
    process.exit(4);
  }

  if (!isValidTokenResponse(respObj)) {
    log(`Error: invalid token response from server (missing access_token or expires_in).`);
    dbg(`Response: ${JSON.stringify(respObj)}`);
    log(`Credentials file NOT modified.`);
    process.exit(4);
  }

  // ── 3. Safe write ──────────────────────────────────────────────────────────

  const newOauth   = mergeRefreshed(oldOauth, respObj, nowMs);
  const newCreds   = { ...fullCreds, claudeAiOauth: newOauth };
  const newJson    = JSON.stringify(newCreds, null, 2);

  // Step a: Preserve original file mode
  let originalMode;
  try {
    originalMode = statSync(credsPath).mode;
  } catch {
    originalMode = 0o600;
  }

  // Step b: .bak copy (overwrite previous backup)
  try {
    copyFileSync(credsPath, bakPath);
    dbg(`Backup written: ${bakPath}`);
  } catch (err) {
    // Non-fatal — log and continue; the .tmp rename is still atomic
    warn(`Could not write .bak file: ${err.message}`);
  }

  // Step c: Write to .tmp then atomically rename
  try {
    writeFileSync(tmpPath, newJson, { encoding: 'utf8', mode: originalMode });
    renameSync(tmpPath, credsPath);
  } catch (err) {
    // Try to clean up the .tmp file if it exists
    try { if (existsSync(tmpPath)) writeFileSync(tmpPath, ''); } catch { /* ignore */ }
    log(`Error: failed to write credentials file: ${err.message}`);
    log(`The original credentials file is UNTOUCHED. Recovery backup: ${bakPath}`);
    process.exit(4);
  }

  log(`Credentials updated. New expiry: ${new Date(newOauth.expiresAt).toLocaleString()}`);
  dbg(`New access token:  ${redactToken(newOauth.accessToken)}`);
  dbg(`New refresh token: ${redactToken(newOauth.refreshToken)}`);

  // ── 4. Push to device ─────────────────────────────────────────────────────

  if (noPush) {
    log(`Summary: refreshed | no-push | expires ${new Date(newOauth.expiresAt).toLocaleString()}`);
    process.exit(0);
  }

  await doPush({
    access:    newOauth.accessToken,
    refresh:   newOauth.refreshToken,
    expiresAt: newOauth.expiresAt,
    label,
  });

  log(`Summary: refreshed | push-attempted | expires ${new Date(newOauth.expiresAt).toLocaleString()}`);
  process.exit(0);
}

// ─── Push helper ──────────────────────────────────────────────────────────────

async function doPush({ access, refresh, expiresAt, label }) {
  let pushTokens;
  try {
    ({ pushTokens } = await import('./push.js'));
  } catch (err) {
    warn(`Cannot load push.js: ${err.message} — device push skipped.`);
    return;
  }

  const payload = {
    label,
    access,
    refresh,
    expires_at: toExpiresAtSeconds(expiresAt),
  };

  dbg(`Pushing tokens to device (label="${label}", expires_at=${payload.expires_at})…`);

  let result;
  try {
    result = await pushTokens(payload);
  } catch (err) {
    warn(`Device push threw unexpectedly: ${err.message}`);
    return;
  }

  if (result.ok) {
    log(`Device push succeeded via ${result.mechanism} (${result.port || result.url || ''})`);
  } else {
    warn(`Device push failed (device may be offline — this is non-fatal):`);
    if (result.serial) warn(`  serial: ${result.serial}`);
    if (result.http)   warn(`  http:   ${result.http}`);
    if (!result.serial && !result.http) warn(`  ${result.reason}`);
  }
}

// ─── Entry point ──────────────────────────────────────────────────────────────

main().catch(err => {
  console.error(`[${new Date().toISOString()}] Unhandled error: ${err.message}`);
  if (isDebug) console.error(err.stack);
  process.exit(4);
});

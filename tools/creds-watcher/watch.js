#!/usr/bin/env node
/**
 * watch.js — EspScreen credentials watcher daemon
 *
 * Watches ~/.claude/.credentials.json for changes and pushes new Claude OAuth
 * tokens to the ESP32 via USB serial or WiFi push.
 *
 * Usage:
 *   node watch.js             — run forever (daemon mode)
 *   node watch.js --once      — push once and exit
 *   node watch.js --label X   — push to profile label X (default: "Default")
 *   node watch.js --debug     — verbose logging
 *
 * Single-instance: writes a PID file to ~/.espscreen/watcher.pid.
 * State cache:    ~/.espscreen/state.json — tracks last-pushed token hash
 *                  (idempotency — skips push if tokens haven't changed)
 *
 * Device config:  ~/.espscreen/devices.json
 *   Format:  { "Default": "10.0.0.88" }
 *        or  { "Default": { "ip": "10.0.0.88", "secret": "mysecret" } }
 *        or  { "*": "10.0.0.88" }  (wildcard — match any label)
 */

import chokidar from 'chokidar';
import { readFileSync, writeFileSync, existsSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import os from 'node:os';
import { parseArgs } from 'node:util';
import { pushTokens } from './push.js';

// ─── CLI args ─────────────────────────────────────────────────────────────────

const { values: args } = parseArgs({
  options: {
    once:   { type: 'boolean', default: false },
    label:  { type: 'string',  default: 'Default' },
    debug:  { type: 'boolean', default: false },
    help:   { type: 'boolean', default: false, short: 'h' },
  },
  allowPositionals: false,
});

if (args.help) {
  console.log(`
espscreen-creds-watcher — push Claude tokens to ESP32 on credentials change

Usage:
  node watch.js [options]

Options:
  --once           Push current tokens once and exit
  --label <name>   Target profile label (default: "Default")
  --debug          Verbose logging
  -h, --help       Show help

Device config (WiFi push):
  Create ~/.espscreen/devices.json:
    { "Default": "10.0.0.88" }
    { "Default": { "ip": "10.0.0.88", "secret": "mysecret" } }

Auto-start (Windows Task Scheduler):
  See README.md for Task Scheduler XML template.
`);
  process.exit(0);
}

// ─── Paths ────────────────────────────────────────────────────────────────────

const CREDS_FILE  = join(os.homedir(), '.claude', '.credentials.json');
const STATE_DIR   = join(os.homedir(), '.espscreen');
const STATE_FILE  = join(STATE_DIR, 'state.json');
const PID_FILE    = join(STATE_DIR, 'watcher.pid');

function ensureStateDir() {
  if (!existsSync(STATE_DIR)) mkdirSync(STATE_DIR, { recursive: true });
}

// ─── Logging ──────────────────────────────────────────────────────────────────

function log(msg, ...extra) {
  const ts = new Date().toISOString();
  console.log(`[${ts}] ${msg}`, ...extra);
}

function dbg(msg, ...extra) {
  if (args.debug) log(`[dbg] ${msg}`, ...extra);
}

// ─── Credentials reader ───────────────────────────────────────────────────────

function readCredentials() {
  if (!existsSync(CREDS_FILE)) {
    throw new Error(`Credentials file not found: ${CREDS_FILE}`);
  }
  const raw = readFileSync(CREDS_FILE, 'utf8');
  const json = JSON.parse(raw);

  const oauth = json.claudeAiOauth;
  if (!oauth || !oauth.accessToken || !oauth.refreshToken) {
    throw new Error(
      'claudeAiOauth not found or incomplete in credentials file.\n' +
      'Expected: { accessToken, refreshToken, expiresAt }.\n' +
      'Run `claude login` to refresh credentials.'
    );
  }

  return {
    label:      args.label,
    access:     oauth.accessToken,
    refresh:    oauth.refreshToken,
    expires_at: oauth.expiresAt,  // ms
  };
}

// ─── State / idempotency ──────────────────────────────────────────────────────

function loadState() {
  try {
    if (existsSync(STATE_FILE)) return JSON.parse(readFileSync(STATE_FILE, 'utf8'));
  } catch { /* ignore */ }
  return {};
}

function saveState(state) {
  ensureStateDir();
  writeFileSync(STATE_FILE, JSON.stringify(state, null, 2));
}

function tokenHash(creds) {
  // Simple fingerprint — first+last 8 chars of access token + expires_at
  const a = creds.access;
  return `${a.slice(0, 8)}..${a.slice(-8)}@${creds.expires_at}`;
}

// ─── PID file (single-instance guard) ────────────────────────────────────────

function writePid() {
  ensureStateDir();
  writeFileSync(PID_FILE, String(process.pid));
}

function clearPid() {
  try { writeFileSync(PID_FILE, ''); } catch { /* ignore */ }
}

function checkSingleInstance() {
  if (!existsSync(PID_FILE)) return;
  const pidStr = readFileSync(PID_FILE, 'utf8').trim();
  if (!pidStr) return;
  const pid = parseInt(pidStr, 10);
  if (!pid) return;
  try {
    process.kill(pid, 0);  // throws if process doesn't exist
    log(`Warning: another instance appears to be running (PID ${pid}). Continuing anyway.`);
  } catch {
    // Process doesn't exist — stale PID file, safe to proceed
    dbg(`Stale PID file (${pid}) — overwriting`);
  }
}

// ─── Core push logic ──────────────────────────────────────────────────────────

async function maybePush(reason) {
  log(`Trigger: ${reason}`);

  let creds;
  try {
    creds = readCredentials();
  } catch (err) {
    log(`Error reading credentials: ${err.message}`);
    return;
  }

  const hash = tokenHash(creds);
  const state = loadState();

  if (state.lastHash === hash && !args.once) {
    dbg(`Tokens unchanged (hash=${hash}) — skipping push`);
    return;
  }

  const expiresDate = new Date(creds.expires_at).toISOString();
  log(`Pushing tokens to ESP32 (label="${creds.label}", expires=${expiresDate})`);

  const result = await pushTokens(creds);

  if (result.ok) {
    log(`Push succeeded via ${result.mechanism} (${result.port || result.url || ''})`);
    saveState({ ...state, lastHash: hash, lastPushAt: new Date().toISOString() });
  } else {
    log(`Push failed:`);
    if (result.serial) log(`  serial: ${result.serial}`);
    if (result.http)   log(`  http:   ${result.http}`);
    if (!result.serial && !result.http) log(`  ${result.reason}`);
    log(`Tokens NOT pushed — will retry on next file change`);
  }
}

// ─── Daemon mode ──────────────────────────────────────────────────────────────

async function runDaemon() {
  checkSingleInstance();
  writePid();
  process.on('exit', clearPid);
  process.on('SIGINT', () => { clearPid(); process.exit(0); });
  process.on('SIGTERM', () => { clearPid(); process.exit(0); });

  log(`espscreen-creds-watcher starting (daemon mode, label="${args.label}")`);
  log(`Watching: ${CREDS_FILE}`);

  // Initial push on startup
  await maybePush('startup');

  // Watch for file changes
  const watcher = chokidar.watch(CREDS_FILE, {
    persistent: true,
    usePolling: true,       // more reliable on Windows network drives / AppData
    interval: 5000,         // poll every 5s as fs.watch backup
    awaitWriteFinish: {
      stabilityThreshold: 500,
      pollInterval: 100,
    },
  });

  watcher.on('change', () => maybePush('file changed'));
  watcher.on('error',  (err) => log(`Watcher error: ${err.message}`));

  // Fallback poll every 60s (handles cases where chokidar misses events)
  setInterval(() => maybePush('periodic poll'), 60_000);

  log('Watcher running. Press Ctrl+C to stop.');
}

// ─── Once mode ────────────────────────────────────────────────────────────────

async function runOnce() {
  log(`espscreen-creds-watcher (--once mode, label="${args.label}")`);
  await maybePush('one-shot');
  log('Done.');
  process.exit(0);
}

// ─── Entry point ──────────────────────────────────────────────────────────────

if (args.once) {
  runOnce();
} else {
  runDaemon();
}

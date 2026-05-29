/**
 * push.js — Shared token-push logic for espscreen-creds-watcher.
 *
 * Exports pushTokens({ label, access, refresh, expires_at }) which tries:
 *   1. USB serial  — if an ESP32 is plugged in (auto-detected by VID/PID)
 *   2. WiFi HTTP   — if a device IP is found in ~/.espscreen/devices.json
 *
 * Uses the first mechanism that succeeds; skips both gracefully if neither
 * is available (just logs).
 */

import { SerialPort, ReadlineParser } from 'serialport';
import { readFileSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import os from 'node:os';

// ─── Config paths ─────────────────────────────────────────────────────────────

const DEVICES_FILE = join(os.homedir(), '.espscreen', 'devices.json');

// ─── Known ESP32 VID/PID combos (same as provision.js) ───────────────────────

const ESP32_VIDS = new Set([
  '1a86', // WCH CH340
  '10c4', // Silicon Labs CP210x
  '0403', // FTDI FT232
  '303a', // Espressif built-in USB
]);

function isEsp32Port(port) {
  const vid = (port.vendorId || '').toLowerCase();
  return ESP32_VIDS.has(vid);
}

// ─── Serial helpers ───────────────────────────────────────────────────────────

function openPort(path) {
  return new Promise((resolve, reject) => {
    const port = new SerialPort({ path, baudRate: 115200 });
    port.on('open', () => resolve(port));
    port.on('error', reject);
  });
}

function sendCommand(port, parser, cmd, timeoutMs = 2000) {
  return new Promise((resolve) => {
    const lines = [];
    const cmdTrimmed = cmd.trim();
    const onLine = (line) => {
      const l = line.trim();
      if (!l || l === cmdTrimmed || l.startsWith('> ')) return;
      lines.push(l);
    };
    parser.on('data', onLine);
    port.write(cmd + '\n', 'utf8');
    setTimeout(() => {
      parser.off('data', onLine);
      resolve(lines);
    }, timeoutMs);
  });
}

// ─── USB serial push ──────────────────────────────────────────────────────────

async function pushViaSerial(tokens) {
  const ports = await SerialPort.list();
  const candidates = ports.filter(isEsp32Port);

  if (candidates.length === 0) {
    return { ok: false, reason: 'no ESP32 serial port detected' };
  }

  // Pick highest COM number (Windows convention)
  candidates.sort((a, b) => {
    const na = parseInt(a.path.replace(/\D/g, ''), 10) || 0;
    const nb = parseInt(b.path.replace(/\D/g, ''), 10) || 0;
    return nb - na;
  });

  const portInfo = candidates[0];
  let port;
  try {
    port = await openPort(portInfo.path);
  } catch (err) {
    return { ok: false, reason: `cannot open ${portInfo.path}: ${err.message}` };
  }

  const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

  // Flush any partial buffer
  port.write('\n');
  await new Promise(r => setTimeout(r, 600));

  // Ensure profile exists — try to add it (will fail silently if it exists)
  await sendCommand(port, parser, `claude profile add "${tokens.label}"`, 1000);

  // Set tokens: claude token set "<label>" <access> <refresh> <expires_sec>
  const expiresSec = tokens.expires_at > 1e11
    ? Math.floor(tokens.expires_at / 1000)
    : Math.floor(tokens.expires_at);

  const cmd = `claude token set "${tokens.label}" ${tokens.access} ${tokens.refresh} ${expiresSec}`;
  const resp = await sendCommand(port, parser, cmd, 2000);

  port.close();

  const ok = resp.some(l => l.includes('Tokens set') || l.includes('tokens set'));
  if (ok) {
    return { ok: true, mechanism: 'serial', port: portInfo.path };
  } else {
    return { ok: false, reason: `serial response: ${resp.join(' | ')}` };
  }
}

// ─── WiFi HTTP push ───────────────────────────────────────────────────────────

function loadDevices() {
  if (!existsSync(DEVICES_FILE)) return {};
  try {
    return JSON.parse(readFileSync(DEVICES_FILE, 'utf8'));
  } catch {
    return {};
  }
}

async function pushViaHttp(tokens) {
  const devices = loadDevices();
  const entry = devices[tokens.label] || devices['*'];

  if (!entry) {
    return { ok: false, reason: `no device IP configured for label '${tokens.label}' in ${DEVICES_FILE}` };
  }

  const ip = typeof entry === 'string' ? entry : entry.ip;
  const secret = typeof entry === 'object' ? (entry.secret || '') : '';
  const url = `http://${ip}:8080/api/claude/tokens`;

  const body = JSON.stringify({
    label:      tokens.label,
    access:     tokens.access,
    refresh:    tokens.refresh,
    expires_at: tokens.expires_at,
  });

  const headers = { 'Content-Type': 'application/json' };
  if (secret) headers['X-EspScreen-Secret'] = secret;

  try {
    const resp = await fetch(url, { method: 'POST', headers, body, signal: AbortSignal.timeout(8000) });
    if (resp.ok) {
      return { ok: true, mechanism: 'http', url };
    } else {
      const text = await resp.text().catch(() => '');
      return { ok: false, reason: `HTTP ${resp.status}: ${text.slice(0, 100)}` };
    }
  } catch (err) {
    return { ok: false, reason: `fetch failed: ${err.message}` };
  }
}

// ─── Main export ──────────────────────────────────────────────────────────────

/**
 * Push tokens to the ESP32 via USB serial first, then WiFi HTTP as fallback.
 * Returns { ok, mechanism, details } — never throws.
 */
export async function pushTokens(tokens) {
  // Attempt USB serial
  const serialResult = await pushViaSerial(tokens).catch(err => ({
    ok: false, reason: `serial error: ${err.message}`
  }));

  if (serialResult.ok) {
    return serialResult;
  }

  // Attempt WiFi HTTP
  const httpResult = await pushViaHttp(tokens).catch(err => ({
    ok: false, reason: `http error: ${err.message}`
  }));

  if (httpResult.ok) {
    return httpResult;
  }

  // Both failed
  return {
    ok: false,
    serial: serialResult.reason,
    http: httpResult.reason,
  };
}

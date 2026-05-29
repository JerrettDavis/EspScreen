#!/usr/bin/env node
/**
 * EspScreen Provisioner — push Claude OAuth credentials to ESP32 over USB serial
 * Usage: node provision.js [--port COM20] [--label "Default"] [--credentials <path>]
 *        [--wifi-ssid <ssid> --wifi-pass <pass>] [--list-ports] [--help]
 */

import { SerialPort, ReadlineParser } from 'serialport';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import os from 'node:os';
import { parseArgs } from 'node:util';

// ─── CLI args ────────────────────────────────────────────────────────────────

const { values: args, positionals } = parseArgs({
  options: {
    port:        { type: 'string' },
    label:       { type: 'string',  default: 'Default' },
    credentials: { type: 'string' },
    'wifi-ssid': { type: 'string' },
    'wifi-pass': { type: 'string' },
    'list-ports':{ type: 'boolean', default: false },
    reset:       { type: 'boolean', default: false },
    help:        { type: 'boolean', default: false, short: 'h' },
  },
  allowPositionals: true,
});

if (args.help) {
  console.log(`
EspScreen Provisioner — push Claude OAuth creds to ESP32 over USB serial

Usage:
  node provision.js [options]

Options:
  --port <COMx>          Override auto-detected serial port
  --label <name>         Claude profile label (default: "Default")
  --credentials <path>   Override credentials file path
  --wifi-ssid <ssid>     WiFi SSID to provision (requires --wifi-pass)
  --wifi-pass <pass>     WiFi password
  --list-ports           Enumerate serial ports and exit
  --reset                Power-cycle the board via DTR/RTS before provisioning
                         (useful when board is in a crashed/wedged state)
  -h, --help             Show this help

Examples:
  node provision.js
  node provision.js --port COM20
  node provision.js --label "Work Account"
  node provision.js --label "Work" --wifi-ssid "OfficeNet" --wifi-pass "secret123"
  node provision.js --list-ports
  node provision.js --reset
`);
  process.exit(0);
}

// ─── Known ESP32 VID/PID combos ─────────────────────────────────────────────

const ESP32_VIDS = new Set([
  '1a86', // WCH CH340 (most common clone boards)
  '10c4', // Silicon Labs CP210x
  '0403', // FTDI FT232
  '303a', // Espressif built-in USB (ESP32-S3, C3, etc.)
]);

function isEsp32Port(port) {
  const vid = (port.vendorId || '').toLowerCase();
  return ESP32_VIDS.has(vid);
}

// ─── Port discovery ──────────────────────────────────────────────────────────

async function findEsp32Port() {
  const ports = await SerialPort.list();

  if (args['list-ports']) {
    console.log('\nAvailable serial ports:\n');
    for (const p of ports) {
      const esp = isEsp32Port(p) ? '  ← ESP32 candidate' : '';
      const vid = p.vendorId ? `VID:${p.vendorId.toUpperCase()} PID:${p.productId?.toUpperCase()}` : 'no VID';
      console.log(`  ${p.path.padEnd(10)} ${p.friendlyName || p.manufacturer || 'unknown'}  [${vid}]${esp}`);
    }
    console.log('');
    process.exit(0);
  }

  const candidates = ports.filter(isEsp32Port);
  if (candidates.length === 0) return null;

  // Pick highest COM number (Windows DevKits often appear at COM20+)
  candidates.sort((a, b) => {
    const numA = parseInt(a.path.replace(/\D/g, ''), 10) || 0;
    const numB = parseInt(b.path.replace(/\D/g, ''), 10) || 0;
    return numB - numA;
  });

  return candidates[0];
}

// ─── Credentials ─────────────────────────────────────────────────────────────

function loadCredentials(credPath) {
  const raw = readFileSync(credPath, 'utf8');
  const json = JSON.parse(raw);

  const oauth = json.claudeAiOauth;
  if (!oauth || !oauth.accessToken || !oauth.refreshToken) {
    throw new Error(
      'claudeAiOauth not found or incomplete in credentials file.\n' +
      'Expected keys: accessToken, refreshToken, expiresAt.\n' +
      'Run `claude login` to refresh credentials.'
    );
  }

  return {
    accessToken:  oauth.accessToken,
    refreshToken: oauth.refreshToken,
    expiresAt:    oauth.expiresAt,   // ms — firmware auto-detects ms vs sec
    expiresAtSec: Math.floor(oauth.expiresAt / 1000),
  };
}

function redact(token) {
  if (!token || token.length < 10) return '[empty]';
  return token.slice(0, 6) + '...' + token.slice(-6);
}

function formatExpiry(expiresAtMs) {
  const msLeft = expiresAtMs - Date.now();
  if (msLeft <= 0) return 'EXPIRED';
  const h = Math.floor(msLeft / 3_600_000);
  const m = Math.floor((msLeft % 3_600_000) / 60_000);
  return `${h}h ${m}m`;
}

// ─── Serial helpers ───────────────────────────────────────────────────────────

function openPort(path) {
  return new Promise((resolve, reject) => {
    const port = new SerialPort({
      path,
      baudRate: 115200,
      dataBits: 8,
      parity:   'none',
      stopBits: 1,
      // Do NOT toggle DTR/RTS on open — would reset the board unintentionally.
      // Use --reset flag if an explicit power-cycle is desired.
    });
    port.on('open', () => resolve(port));
    port.on('error', reject);
  });
}

/**
 * Power-cycle the ESP32 via DTR/RTS signals, mimicking the esptool reset sequence.
 *   1. DTR=false, RTS=true  → hold EN (reset) low for 100ms
 *   2. DTR=false, RTS=false → release EN; board boots
 *   3. Wait 600ms for the firmware to reach the serial CLI prompt
 *
 * Only called when --reset is passed. Default provisioning does NOT reset.
 */
function resetBoard(port) {
  return new Promise((resolve, reject) => {
    console.log('  --reset: toggling DTR/RTS to power-cycle board...');
    // Step 1: Assert reset (RTS=true → EN pulled low on most ESP32 dev kits)
    port.set({ dtr: false, rts: true }, (err) => {
      if (err) { reject(err); return; }
      setTimeout(() => {
        // Step 2: Release reset
        port.set({ dtr: false, rts: false }, (err2) => {
          if (err2) { reject(err2); return; }
          console.log('  --reset: board released, waiting 600ms for boot...');
          // Step 3: Wait for firmware to boot and reach CLI prompt
          setTimeout(resolve, 600);
        });
      }, 100);
    });
  });
}

/**
 * Send a command and collect response lines for `timeoutMs`.
 * Filters out our own echo (firmware echoes commands back).
 * Returns array of non-empty response lines.
 */
function sendCommand(port, parser, cmd, timeoutMs = 1500) {
  return new Promise((resolve) => {
    const lines = [];
    const cmdTrimmed = cmd.trim();

    const onLine = (line) => {
      const l = line.trim();
      if (!l) return;
      // Filter echo: skip lines that exactly match our command (or start with the cmd prefix)
      if (l === cmdTrimmed || l.startsWith('> ')) return;
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

// ─── Main ────────────────────────────────────────────────────────────────────

const STATUS = {
  port:        null,
  creds:       null,
  profile:     null,
  tokens:      null,
  wifi:        null,
  poll:        null,
};

async function main() {
  // ── 1. Port discovery (--list-ports exits here if requested) ────────────
  let portInfo;
  if (args.port) {
    // Manual override — just use it, trust the user
    portInfo = { path: args.port, friendlyName: args.port, vendorId: 'manual' };
  } else {
    portInfo = await findEsp32Port();  // exits if --list-ports
    if (!portInfo) {
      console.error('\n✗ No ESP32 device found on any serial port.');
      console.error('  Known VIDs: CH340 (1A86), CP210x (10C4), FTDI (0403), Espressif (303A)');
      console.error('  Use --port COMxx to specify manually, or --list-ports to enumerate.');
      process.exit(1);
    }
  }

  const portLabel = portInfo.friendlyName
    ? `${portInfo.path} (${portInfo.friendlyName})`
    : portInfo.path;

  console.log(`\n  Using port: ${portLabel}`);

  // ── 2. Load credentials ──────────────────────────────────────────────────
  const credPath = args.credentials
    ?? join(os.homedir(), '.claude', '.credentials.json');

  let creds;
  try {
    creds = loadCredentials(credPath);
  } catch (err) {
    console.error(`\n✗ Failed to load credentials: ${err.message}`);
    process.exit(1);
  }

  STATUS.creds = `expires in ${formatExpiry(creds.expiresAt)}`;
  console.log(`\n  Credentials loaded — accessToken: ${redact(creds.accessToken)}`);
  console.log(`  RefreshToken: ${redact(creds.refreshToken)}`);
  console.log(`  Expires: ${STATUS.creds}`);

  // ── 3. Open serial ───────────────────────────────────────────────────────
  let port;
  try {
    port = await openPort(portInfo.path);
  } catch (err) {
    console.error(`\n✗ Cannot open ${portInfo.path}: ${err.message}`);
    console.error('  Is the port in use by another program (e.g. VS Code Serial Monitor)?');
    process.exit(1);
  }

  STATUS.port = portLabel;

  const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

  // ── 3b. Optional board reset via DTR/RTS ─────────────────────────────
  if (args.reset) {
    try {
      await resetBoard(port);
      console.log('  Board reset complete.');
    } catch (err) {
      console.warn(`  Warning: --reset failed (${err.message}). Continuing anyway.`);
    }
  }

  // ── 4. Settle + wake ─────────────────────────────────────────────────
  // Send a blank line to flush any partial command in the board's buffer,
  // then wait for the board to process it before we start issuing commands.
  port.write('\n');
  await new Promise(r => setTimeout(r, 800));

  // ── 5. Profile management ─────────────────────────────────────────────
  const label = args.label;

  console.log(`\n  Checking existing profiles...`);
  const listLines = await sendCommand(port, parser, 'claude profile list', 1500);
  console.log('  Board response:');
  for (const l of listLines) console.log(`    ${l}`);

  // Check if our label already exists in the profile list
  const profileExists = listLines.some(l =>
    l.toLowerCase().includes(label.toLowerCase())
  );

  if (profileExists) {
    console.log(`\n  Profile "${label}" already exists — skipping add, will update tokens.`);
  } else {
    console.log(`\n  Adding profile "${label}"...`);
    const addLines = await sendCommand(port, parser, `claude profile add "${label}"`, 1500);
    console.log('  Board response:');
    for (const l of addLines) console.log(`    ${l}`);

    const addOk = addLines.some(l => /added|ok|success|created/i.test(l));
    if (!addOk && addLines.length > 0) {
      const hasErr = addLines.some(l => /err|fail|invalid/i.test(l));
      if (hasErr) {
        console.error(`\n✗ Profile add failed. Board said: ${addLines.join('; ')}`);
        await closePort(port);
        process.exit(1);
      }
    }
    STATUS.profile = `"${label}" added`;
  }

  // ── 6. Set tokens ─────────────────────────────────────────────────────
  console.log(`\n  Setting tokens on profile "${label}"...`);
  const tokenCmd = `claude token set "${label}" ${creds.accessToken} ${creds.refreshToken} ${creds.expiresAtSec}`;
  const tokenLines = await sendCommand(port, parser, tokenCmd, 2000);
  console.log('  Board response:');
  for (const l of tokenLines) console.log(`    ${l}`);

  const tokenOk = tokenLines.some(l => /set|ok|success|stored|saved/i.test(l));
  const tokenErr = tokenLines.some(l => /err|fail|invalid/i.test(l));
  if (tokenErr) {
    console.error(`\n✗ Token set failed. Board said: ${tokenLines.join('; ')}`);
    await closePort(port);
    process.exit(1);
  }
  STATUS.tokens = tokenOk ? 'set' : (tokenLines.length > 0 ? 'sent (no explicit OK)' : 'sent (no response)');

  // ── 7. Activate profile ────────────────────────────────────────────────
  console.log(`\n  Activating profile "${label}"...`);
  const useLines = await sendCommand(port, parser, `claude profile use "${label}"`, 1500);
  console.log('  Board response:');
  for (const l of useLines) console.log(`    ${l}`);
  STATUS.profile = `"${label}" active`;

  // ── 8. Optional WiFi ──────────────────────────────────────────────────
  const wifiSsid = args['wifi-ssid'];
  const wifiPass = args['wifi-pass'];

  if (wifiSsid && wifiPass) {
    console.log(`\n  Provisioning WiFi: "${wifiSsid}"...`);
    const wifiLines = await sendCommand(port, parser, `wifi add "${wifiSsid}" "${wifiPass}"`, 2000);
    console.log('  Board response:');
    for (const l of wifiLines) console.log(`    ${l}`);
    const wifiOk = wifiLines.some(l => /add|ok|success|saved|updated/i.test(l));
    STATUS.wifi = wifiOk ? `"${wifiSsid}" saved` : `"${wifiSsid}" sent`;
  } else if (wifiSsid || wifiPass) {
    console.warn('\n  Warning: --wifi-ssid and --wifi-pass must be used together. WiFi skipped.');
  }

  // ── 9. Poll ───────────────────────────────────────────────────────────
  console.log(`\n  Starting poll (waiting up to 12s for HTTPS handshake)...`);
  const pollLines = await sendCommand(port, parser, 'claude poll', 12000);
  console.log('  Board response:');
  for (const l of pollLines) console.log(`    ${l}`);

  const poll401 = pollLines.some(l => /401|unauthorized|invalid.token|expired/i.test(l));
  const pollOk  = pollLines.some(l => /poll.ok|ok|success|200|data|fetched/i.test(l));
  const pollNoResponse = pollLines.length === 0;

  if (poll401) {
    STATUS.poll = '401 — token rejected';
    console.error('\n✗ Token rejected by Anthropic (401).');
    console.error('  Your credentials may be stale. Try: claude login  (on your host machine)');
    console.error('  Then re-run: node provision.js');
  } else if (pollOk) {
    STATUS.poll = 'OK';
  } else if (pollNoResponse) {
    STATUS.poll = 'no response — board may have rebooted after first poll';
    console.warn('\n  Note: No poll response received. The board may have restarted');
    console.warn('  (first-run firmware behavior). Credentials are stored in NVS');
    console.warn('  and will persist across reboots. Check board serial output to confirm.');
  } else {
    STATUS.poll = `no explicit OK (${pollLines.join('; ').slice(0, 80)})`;
  }

  // ── 10. Summary ───────────────────────────────────────────────────────
  await closePort(port);
  printSummary();

  process.exit(poll401 ? 1 : 0);
}

function printSummary() {
  console.log('\n─────────────────────────────────────────────────');
  const tick = (v) => v ? '✓' : '✗';

  console.log(`  ${tick(STATUS.port)}  Connected to ${STATUS.port ?? 'no port'}`);
  console.log(`  ${tick(STATUS.creds)}  Credentials loaded (${STATUS.creds ?? 'unknown'})`);
  console.log(`  ${tick(STATUS.profile)}  Profile ${STATUS.profile ?? 'not set'}`);
  console.log(`  ${tick(STATUS.tokens)}  Tokens ${STATUS.tokens ?? 'not set'}`);

  if (STATUS.wifi !== null) {
    console.log(`  ${tick(STATUS.wifi)}  WiFi ${STATUS.wifi}`);
  }

  const pollOk = STATUS.poll === 'OK';
  if (STATUS.poll) {
    const pollSuccess = STATUS.poll === 'OK' || STATUS.poll.includes('no response — board may have rebooted');
    console.log(`  ${tick(pollSuccess)}  Poll ${STATUS.poll}`);
  }

  console.log('─────────────────────────────────────────────────\n');
}

function closePort(port) {
  return new Promise((resolve) => {
    if (!port.isOpen) { resolve(); return; }
    port.close((err) => {
      if (err) console.warn(`  Warning: error closing port: ${err.message}`);
      resolve();
    });
  });
}

main().catch(err => {
  console.error(`\n✗ Unexpected error: ${err.message}`);
  process.exit(1);
});

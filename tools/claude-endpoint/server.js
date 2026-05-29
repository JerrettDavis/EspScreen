/**
 * claude-endpoint — Local HTTP server for ESP32 usage widget polling.
 *
 * Serves GET /status.json with Claude Code usage data.
 * Reads credentials from ~/.claude/.credentials.json (claudeAiOauth.accessToken).
 * Optionally pulls Headroom proxy stats from localhost:8787.
 *
 * Port: 8765 (override via PORT env var)
 */

import { createServer } from "node:http";
import { readFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";

const VERSION = "0.1.0";
const PORT = parseInt(process.env.PORT ?? "8766", 10);
const API_URL = "https://api.anthropic.com/api/oauth/usage";
const HEADROOM_URL = "http://127.0.0.1:8787/stats";
const CREDS_PATH = join(
  process.env.CLAUDE_CONFIG_DIR ?? join(homedir(), ".claude"),
  ".credentials.json"
);

// ── Cache state ──────────────────────────────────────────────────────────────

/** Last successfully built payload (raw object, pre-JSON). */
let cachedPayload = null;
/** Unix ms timestamp of the last successful upstream fetch. */
let lastSuccessMs = 0;
/** True when a background refresh is already in-flight. */
let refreshInFlight = false;
/** Last time we re-read credentials from disk. */
let credsCachedAt = 0;
let cachedToken = null;

const CACHE_TTL_MS = 60_000;      // 60 s — keep fresh for ESP32
const STALE_BADGE_MS = 5 * 60_000; // 5 min — show stale badge
const CREDS_REREAD_MS = 5 * 60_000; // 5 min — re-read creds file

// ── Credentials ──────────────────────────────────────────────────────────────

function readAccessToken() {
  const now = Date.now();
  if (cachedToken && (now - credsCachedAt) < CREDS_REREAD_MS) {
    return cachedToken;
  }

  try {
    const raw = readFileSync(CREDS_PATH, "utf-8");
    const creds = JSON.parse(raw);
    const oauth = creds?.claudeAiOauth;
    if (!oauth?.accessToken) {
      log("WARN", "credentials.json found but claudeAiOauth.accessToken is missing");
      cachedToken = null;
      return null;
    }
    // Check expiry — treat token as invalid if expired >1 min ago
    if (oauth.expiresAt && oauth.expiresAt < now - 60_000) {
      log("WARN", "OAuth token is expired (expiresAt=" + new Date(oauth.expiresAt).toISOString() + ")");
      cachedToken = null;
      return null;
    }
    cachedToken = oauth.accessToken;
    credsCachedAt = now;
    return cachedToken;
  } catch (err) {
    if (err.code === "ENOENT") {
      log("ERROR", "credentials file not found: " + CREDS_PATH);
    } else {
      log("ERROR", "failed to read credentials: " + err.message);
    }
    cachedToken = null;
    return null;
  }
}

// ── Upstream API fetch ────────────────────────────────────────────────────────

async function fetchUsageApi(token) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), 8_000);
  try {
    const res = await fetch(API_URL, {
      method: "GET",
      headers: {
        Authorization: `Bearer ${token}`,
        "anthropic-beta": "oauth-2025-04-20",
        "Content-Type": "application/json",
      },
      signal: ctrl.signal,
    });
    clearTimeout(timer);

    if (res.status === 429) {
      const retryAfter = res.headers.get("retry-after");
      log("WARN", "API rate limited. Retry-After: " + (retryAfter ?? "none"));
      return null;
    }
    if (!res.ok) {
      log("WARN", "API returned HTTP " + res.status);
      return null;
    }
    return await res.json();
  } catch (err) {
    clearTimeout(timer);
    log("WARN", "API fetch failed: " + err.message);
    return null;
  }
}

async function fetchHeadroom() {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), 1_000);
  try {
    const res = await fetch(HEADROOM_URL, { signal: ctrl.signal });
    clearTimeout(timer);
    if (!res.ok) return null;
    return await res.json();
  } catch {
    clearTimeout(timer);
    return null;
  }
}

// ── Payload builder ───────────────────────────────────────────────────────────

/**
 * Convert a RateLimit object from the API into our contract shape.
 * API shape: { utilization: 0.65, resets_at: "2026-05-28T12:30:00Z" }
 */
function buildRateEntry(rl) {
  if (!rl) return null;
  const resetsAt = rl.resets_at ?? null;
  let resetsInSec = null;
  if (resetsAt) {
    resetsInSec = Math.max(0, Math.round((new Date(resetsAt).getTime() - Date.now()) / 1000));
  }
  return {
    utilization: rl.utilization ?? null,
    resets_at: resetsAt,
    resets_in_sec: resetsInSec,
  };
}

function buildHeadroomEntry(raw) {
  if (!raw) return { enabled: false };
  return {
    enabled: true,
    tokens_saved: (raw.tokens?.saved ?? 0) + (raw.tokens?.cli_tokens_avoided ?? 0),
    compression_pct: raw.tokens?.savings_percent ?? 0,
    cost_saved_usd: raw.cost?.savings_usd ?? 0,
  };
}

async function buildPayload() {
  const token = readAccessToken();
  if (!token) {
    return null; // signal: no credentials
  }

  const [usageRaw, headroomRaw] = await Promise.all([
    fetchUsageApi(token),
    fetchHeadroom(),
  ]);

  if (!usageRaw) return null; // upstream failed

  const now = Date.now();
  const payload = {
    ts: Math.floor(now / 1000),
    rates: {
      five_hour: buildRateEntry(usageRaw.five_hour),
      seven_day: buildRateEntry(usageRaw.seven_day),
    },
    session: {
      model: null,
      cost_usd: null,
      context_used_pct: null,
      cache_ttl_sec: null,
    },
    headroom: buildHeadroomEntry(headroomRaw),
    stale: false,
  };

  return payload;
}

// ── Refresh logic (stale-while-revalidate) ────────────────────────────────────

async function backgroundRefresh() {
  if (refreshInFlight) return;
  refreshInFlight = true;
  try {
    const payload = await buildPayload();
    if (payload) {
      cachedPayload = payload;
      lastSuccessMs = Date.now();
      log("INFO", "cache refreshed");
    } else {
      log("WARN", "background refresh produced no payload (credentials or API issue)");
    }
  } finally {
    refreshInFlight = false;
  }
}

function isCacheFresh() {
  return lastSuccessMs > 0 && (Date.now() - lastSuccessMs) < CACHE_TTL_MS;
}

function maybeKickRefresh() {
  if (!isCacheFresh() && !refreshInFlight) {
    backgroundRefresh().catch((err) => log("ERROR", "refresh error: " + err.message));
  }
}

// ── Logging ───────────────────────────────────────────────────────────────────

function log(level, msg) {
  const ts = new Date().toISOString();
  process.stdout.write(`[${ts}] [${level}] ${msg}\n`);
}

function logRequest(req, statusCode, bodyLen) {
  const ts = new Date().toISOString();
  process.stdout.write(`[${ts}] ${req.method} ${req.url} → ${statusCode} (${bodyLen}B)\n`);
}

// ── HTTP server ───────────────────────────────────────────────────────────────

const server = createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host ?? "localhost"}`);
  const path = url.pathname;

  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  res.setHeader("Content-Type", "application/json");

  if (req.method === "OPTIONS") {
    res.writeHead(204);
    res.end();
    logRequest(req, 204, 0);
    return;
  }

  if (req.method !== "GET") {
    const body = JSON.stringify({ error: "method_not_allowed" });
    res.writeHead(405);
    res.end(body);
    logRequest(req, 405, body.length);
    return;
  }

  // ── GET /health ────────────────────────────────────────────────────────────
  if (path === "/health") {
    const body = JSON.stringify({ ok: true, version: VERSION });
    res.writeHead(200);
    res.end(body);
    logRequest(req, 200, body.length);
    return;
  }

  // ── GET /status.json ───────────────────────────────────────────────────────
  if (path === "/status.json") {
    // Kick off a background refresh if stale (don't await — stale-while-revalidate)
    maybeKickRefresh();

    // First-ever request with no cache: must wait for the first fetch
    if (!cachedPayload && lastSuccessMs === 0) {
      // Check if we even have credentials before waiting
      const token = readAccessToken();
      if (!token) {
        const body = JSON.stringify({ error: "no_credentials" });
        res.writeHead(503);
        res.end(body);
        logRequest(req, 503, body.length);
        return;
      }
      // Block on first fetch (ESP32 can wait a few seconds on boot)
      await backgroundRefresh();
    }

    if (!cachedPayload) {
      // Still no payload after first attempt
      const token = readAccessToken();
      if (!token) {
        const body = JSON.stringify({ error: "no_credentials" });
        res.writeHead(503);
        res.end(body);
        logRequest(req, 503, body.length);
        return;
      }
      const body = JSON.stringify({ error: "upstream_unavailable" });
      res.writeHead(503);
      res.end(body);
      logRequest(req, 503, body.length);
      return;
    }

    // Annotate staleness
    const stale = (Date.now() - lastSuccessMs) > STALE_BADGE_MS;
    const payload = { ...cachedPayload, stale };

    const body = JSON.stringify(payload);
    res.writeHead(200);
    res.end(body);
    logRequest(req, 200, body.length);
    return;
  }

  // ── 404 ───────────────────────────────────────────────────────────────────
  const body = JSON.stringify({ error: "not_found" });
  res.writeHead(404);
  res.end(body);
  logRequest(req, 404, body.length);
});

// ── Startup ───────────────────────────────────────────────────────────────────

server.listen(PORT, "0.0.0.0", () => {
  log("INFO", `claude-endpoint v${VERSION} listening on port ${PORT}`);
  log("INFO", `Credentials path: ${CREDS_PATH}`);
  log("INFO", "Endpoints: GET /health  GET /status.json");

  // Verify credentials on startup
  const token = readAccessToken();
  if (!token) {
    log("WARN", "No valid credentials found — /status.json will return 503 until credentials are available");
  } else {
    log("INFO", "Credentials OK — kicking initial background fetch");
    backgroundRefresh().catch((err) => log("ERROR", "initial fetch error: " + err.message));
  }
});

server.on("error", (err) => {
  log("ERROR", "Server error: " + err.message);
  if (err.code === "EADDRINUSE") {
    log("ERROR", `Port ${PORT} is already in use. Set PORT=<other> env var.`);
    process.exit(1);
  }
});

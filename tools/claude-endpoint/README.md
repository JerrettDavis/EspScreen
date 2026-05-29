# claude-endpoint

Local HTTP server that exposes Claude Code usage data as JSON for the EspScreen ESP32 widget.

## Requirements

- Node.js 18+ (no build step, no TypeScript, no external dependencies)
- A valid Claude Code login (credentials at `~/.claude/.credentials.json`)

## Install

```bash
cd C:\git\EspScreen\tools\claude-endpoint
npm install   # no-op: zero dependencies
```

## Run

```bash
npm start
# or
node server.js
```

Override the port:
```bash
PORT=9000 node server.js
```

## Verify

```bash
# Health check
curl http://localhost:8766/health
# → {"ok":true,"version":"0.1.0"}

# Status payload
curl http://localhost:8766/status.json
```

## Endpoints

| Endpoint | Description |
|---|---|
| `GET /health` | Returns `{"ok":true,"version":"..."}` |
| `GET /status.json` | Claude usage JSON contract (see below) |

## JSON contract

```json
{
  "ts": 1716800000,
  "rates": {
    "five_hour": { "utilization": 0.65, "resets_at": "2026-05-28T12:30:00Z", "resets_in_sec": 1234 },
    "seven_day": { "utilization": 0.42, "resets_at": "2026-06-04T08:00:00Z", "resets_in_sec": 567890 }
  },
  "session": {
    "model": null,
    "cost_usd": null,
    "context_used_pct": null,
    "cache_ttl_sec": null
  },
  "headroom": {
    "enabled": true,
    "tokens_saved": 2840,
    "compression_pct": 34,
    "cost_saved_usd": 0.08
  },
  "stale": false
}
```

- `stale: true` if last successful fetch was >5 minutes ago
- `headroom.enabled: false` if Headroom proxy isn't reachable at `localhost:8787`
- `session.*` fields are `null` when no session data is available (shape is stable)

## Error responses

| Condition | HTTP | Body |
|---|---|---|
| No credentials | 503 | `{"error":"no_credentials"}` |
| Upstream API unavailable | 503 | `{"error":"upstream_unavailable"}` |

## Finding your LAN IP for the ESP32

```powershell
ipconfig | findstr IPv4
```

Look for your local network adapter (e.g., `192.168.x.x`). The ESP32 should poll:

```
http://<YOUR_LAN_IP>:8766/status.json
```

Example: `http://192.168.1.42:8766/status.json`

## Caching behaviour

- Data is cached in memory for 60 seconds
- Requests always return immediately (stale-while-revalidate)
- Background refresh runs automatically when the cache expires
- If upstream fails, the last-known payload is served with `stale: true`
- Credentials are re-read from disk every 5 minutes (picks up token refreshes)

## Headroom integration

If the Headroom compression proxy is running at `localhost:8787`, the server automatically includes token savings stats. If it's not running, `headroom: { enabled: false }` is returned without errors.

## Running as a background service (optional)

Use `pm2` or Windows Task Scheduler to keep it alive:

```bash
npm install -g pm2
pm2 start server.js --name claude-endpoint
pm2 save
pm2 startup
```

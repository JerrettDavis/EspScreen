# espscreen-creds-watcher

Watches `~/.claude/.credentials.json` for changes and automatically pushes
new Claude OAuth tokens to the ESP32 board. Keeps the board's tokens fresh
without user intervention.

## How it works

Claude Code automatically refreshes `~/.claude/.credentials.json` when you
actively use it (before expiry). This watcher detects those file changes and
pushes the new tokens to the board immediately via:

1. **USB serial** — if the board is plugged in (auto-detected by VID/PID)
2. **WiFi HTTP** — if the board's IP is configured in `~/.espscreen/devices.json`

The watcher tries serial first, then HTTP. It tracks last-pushed tokens to
avoid redundant pushes (idempotent).

## Install

```bash
cd tools/creds-watcher
npm install
```

## Device config (WiFi push)

Create `~/.espscreen/devices.json`:

```json
{ "Default": "10.0.0.88" }
```

With shared secret (set via `api set-secret <secret>` on the board):

```json
{ "Default": { "ip": "10.0.0.88", "secret": "mysecret" } }
```

Wildcard (match any label):

```json
{ "*": "10.0.0.88" }
```

Find the board's IP: connect to serial and run `wifi status`.

## Usage

```bash
# Run as daemon (watches forever)
npm start

# Push once and exit (for cron / Task Scheduler)
npm run once

# Push to a different profile label
node watch.js --label "Work Account"

# Debug logging
node watch.js --debug
```

## Token Refresh

The creds-watcher package includes a standalone token-refresh tool that proactively
renews the Claude OAuth access token on the **host machine** and then pushes the
fresh tokens to the ESP32 device using the same `push.js` path as the watcher daemon.

### Relationship between refresh.js and watch.js

| Tool | Job |
|------|-----|
| `watch.js` (daemon) | Watches the credentials file for changes written by Claude Code and pushes existing tokens to the device. It does NOT refresh tokens itself. |
| `refresh.js` (one-shot) | Performs an OAuth refresh_token grant, writes new credentials to the host file (atomic safe-write), then pushes the new tokens to the device. Designed for scheduled / headless environments where Claude Code is not running. |

Use `watch.js` when Claude Code is your normal workflow and tokens are refreshed
organically. Use `refresh.js` (via Task Scheduler or cron) when you need the device
to stay in sync on machines where Claude Code may not run for hours.

### Usage

```bash
# Run a refresh (skip if token is still valid; push result to device)
node refresh.js

# Force refresh even if not near expiry
node refresh.js --force

# Refresh host credentials only — skip device push
node refresh.js --no-push

# Refresh for a named profile label (must match devices.json key)
node refresh.js --label "Work Account"

# Change the proactive-refresh window (default: 30 minutes)
node refresh.js --threshold-min 15

# Verbose output
node refresh.js --debug

# npm convenience shortcut
npm run refresh
```

### Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--force` | off | Refresh even if the token is still valid |
| `--no-push` | off | Write the credentials file only; skip device push |
| `--label <name>` | `"Default"` | Profile label passed to the device push |
| `--threshold-min <N>` | `30` | Minutes before expiry to proactively refresh |
| `--debug` | off | Verbose logging (redacted — full tokens never printed) |

### Exit codes

| Code | Meaning |
|------|---------|
| `0` | Success — token was refreshed (or was still valid), push attempted |
| `2` | Credentials file not found — run `claude login` first |
| `3` | No refresh token in credentials — run `claude login` |
| `4` | Refresh failed — **credentials file left completely untouched** |

### Safe-write guarantee

The tool will never corrupt or lose the working credentials file:

1. Before any write, the existing file is copied to `<credsPath>.bak` (overwrites
   any previous backup).
2. The new JSON is written to `<credsPath>.tmp`.
3. `fs.renameSync` atomically replaces the live path — either the old or new file
   is present at every instant; there is no window where the file is absent or
   partially written.
4. Only `claudeAiOauth` fields are updated; all other top-level keys in the JSON
   are preserved exactly as-is.
5. If the network request fails or returns an invalid response, the tool exits with
   code `4` and the credentials file is **never touched**.
6. If a push to the device fails, it is logged as a warning and the tool still
   exits `0` — keeping host credentials fresh is the primary job; device sync is
   best-effort.

### Credentials file location

By default: `~/.claude/.credentials.json`

If the `CLAUDE_CONFIG_DIR` environment variable is set, the tool uses
`$CLAUDE_CONFIG_DIR/.credentials.json` (the variable replaces the `.claude`
directory, not just a path component).

### Task Scheduler setup (Windows)

Run the registration script once (as Administrator):

```powershell
# From the repo root:
.\scripts\register-token-refresh-task.ps1
```

This registers a task named **"EspScreen Claude Token Refresh"** that runs:
- At every user logon
- Every 1 hour (indefinite repetition)

The wrapper script (`scripts/refresh-claude-token.ps1`) logs timestamped output
to `~/.espscreen/token-refresh.log`.

Manual one-shot (no Task Scheduler needed):

```powershell
node tools\creds-watcher\refresh.js --force
```

To unregister the task:

```powershell
Unregister-ScheduledTask -TaskName "EspScreen Claude Token Refresh"
```

---

## Auto-start on Windows (Task Scheduler)

1. Open Task Scheduler → Create Basic Task
2. Name: `EspScreen Creds Watcher`
3. Trigger: At log on (or At startup)
4. Action: Start a program
   - Program: `node`
   - Arguments: `C:\git\EspScreen\tools\creds-watcher\watch.js`
   - Start in: `C:\git\EspScreen\tools\creds-watcher`
5. Conditions: uncheck "Start only if on AC power"
6. Settings: check "Run task as soon as possible after scheduled start missed"

Or via PowerShell (run as Administrator):

```powershell
$action  = New-ScheduledTaskAction -Execute "node" `
             -Argument "C:\git\EspScreen\tools\creds-watcher\watch.js" `
             -WorkingDirectory "C:\git\EspScreen\tools\creds-watcher"
$trigger = New-ScheduledTaskTrigger -AtLogOn
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit 0 -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
Register-ScheduledTask -TaskName "EspScreen Creds Watcher" `
  -Action $action -Trigger $trigger -Settings $settings -RunLevel Highest
```

## Auto-start on Linux (systemd)

```ini
# ~/.config/systemd/user/espscreen-watcher.service
[Unit]
Description=EspScreen Credentials Watcher
After=network.target

[Service]
Type=simple
ExecStart=/usr/bin/node /path/to/EspScreen/tools/creds-watcher/watch.js
WorkingDirectory=/path/to/EspScreen/tools/creds-watcher
Restart=on-failure
RestartSec=10

[Install]
WantedBy=default.target
```

```bash
systemctl --user enable espscreen-watcher
systemctl --user start espscreen-watcher
```

## State files

| File | Purpose |
|------|---------|
| `~/.espscreen/state.json` | Last-pushed token hash + timestamp |
| `~/.espscreen/watcher.pid` | PID file for single-instance guard |
| `~/.espscreen/devices.json` | Board IP mapping (you create this) |

## Board firmware setup

The board must have firmware >= v0.2.0 (the version that includes `api_server`).

```
# Verify API server is running
curl http://10.0.0.88:8080/api/health

# Set shared secret (optional but recommended)
api set-secret mysecret
```

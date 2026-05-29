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

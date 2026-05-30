# tools — Host-Side Utilities

Host-side Node.js utilities for provisioning, credential management, and the
Claude usage data endpoint. All tools require **Node.js >= 18**. No global
installs are required — each tool has its own `package.json`.

---

## Tools

| Tool | One-line purpose | README |
|------|-----------------|--------|
| [`provision`](provision/) | Push Claude OAuth tokens (and optionally WiFi credentials) to the board over USB serial | [provision/README.md](provision/README.md) |
| [`creds-watcher`](creds-watcher/) | Watch `~/.claude/.credentials.json` for refreshed tokens and automatically push them to the board (USB serial or WiFi) | [creds-watcher/README.md](creds-watcher/README.md) |
| [`claude-endpoint`](claude-endpoint/) | Local HTTP server (`localhost:8766`) that exposes Claude Code usage data as JSON for the on-board widget | [claude-endpoint/README.md](claude-endpoint/README.md) |

---

## Quick install

```sh
cd tools/provision      && npm install
cd tools/creds-watcher  && npm install
cd tools/claude-endpoint && npm install   # zero dependencies — no-op
```

---

## Typical workflow

1. Flash the firmware and filesystem (see [firmware/10-launcher/README.md](../firmware/10-launcher/README.md)).
2. Run `tools/provision` once to push your Claude OAuth tokens and (optionally) WiFi:

   ```sh
   cd tools/provision
   node provision.js --wifi-ssid "YourNetwork" --wifi-pass "yourpassword"
   ```

3. Start `tools/creds-watcher` as a background daemon so tokens stay fresh:

   ```sh
   cd tools/creds-watcher
   npm start
   ```

4. Start `tools/claude-endpoint` so the on-board Claude usage widget has data:

   ```sh
   cd tools/claude-endpoint
   npm start
   ```

   Update `config.default.json` (or the board's web portal) to point to
   `http://<YOUR-PC-IP>:8766/status.json`.

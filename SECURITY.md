# Security Policy

## Reporting Vulnerabilities

If you discover a security vulnerability, please **do not open a public GitHub
issue**. Instead, report it privately:

- Open a [GitHub Security Advisory](https://github.com/JerrettDavis/EspScreen/security/advisories/new)
  on this repository (Settings → Security → Advisories → New draft).
- Describe the vulnerability, reproduction steps, and potential impact.

A maintainer will respond within 72 hours to acknowledge receipt and discuss a
fix timeline.

---

## Secrets and Credentials

### What must never be committed

- `.env`, `.env.*`, or any file containing API keys, passwords, or tokens
- `tools/keys/` (gitignored)
- `secrets.json` or `*.local.json` (gitignored)
- `~/.claude/.credentials.json` (lives only on the host machine)

The `.gitignore` in this repository explicitly excludes these patterns. Do not
override them.

### OAuth tokens

Claude OAuth access and refresh tokens are stored in two places only:

1. **Host machine** — `~/.claude/.credentials.json`, written by `claude login`.
   Never committed. The `tools/provision` and `tools/creds-watcher` tools read
   this file and push tokens to the board over USB serial or WiFi.

2. **Device NVS** — stored in the ESP32's Non-Volatile Storage partition,
   which is on-chip flash. NVS is not exported or transmitted; it is only
   accessible via the device API when a valid passcode is set, or over USB
   serial.

Tokens are never logged in full. The provisioning tools redact them to a short
prefix for display (e.g., `eyJ...abc123`).

---

## Device Passcode Model

The device API (port 8080) optionally requires an `X-EspScreen-Secret` header
for all requests. Set a passcode from the web portal Settings tab or via serial:

```
api set-secret <your-secret>
```

When set, unauthenticated API requests receive `401 Unauthorized`. The web
portal (port 80) uses the same secret for its API calls; the portal UI itself
is not password-protected.

The passcode is stored in NVS (not transmitted back via any API endpoint).

**Recommendation:** set a passcode on any device reachable on a shared or
untrusted network.

---

## Network Exposure

- The device does not expose any service to the internet by default; it
  connects to your local WiFi network only.
- The SoftAP (`EspScreen-Setup`) is active only during first-boot setup. After
  WiFi credentials are saved and the device reboots, the AP is disabled.
- No authentication is required on the SoftAP captive portal by design — the
  intent is ease of first-time setup on an isolated temporary network.

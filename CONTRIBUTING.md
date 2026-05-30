# Contributing to EspScreen

Thank you for your interest in contributing. This document covers how to set up
a development environment, build the project, run tests, and submit changes.

---

## Table of Contents

1. [Development Setup](#development-setup)
2. [Building the Firmware](#building-the-firmware)
3. [Running Tests](#running-tests)
4. [Commit Message Convention](#commit-message-convention)
5. [Pull Request Process](#pull-request-process)
6. [Hardware Testing Note](#hardware-testing-note)

---

## Development Setup

### Firmware (C++ / Arduino / PlatformIO)

1. Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html).
   Confirm it works:

   ```sh
   pio --version
   ```

2. Clone the repository:

   ```sh
   git clone https://github.com/JerrettDavis/EspScreen.git
   cd EspScreen
   ```

3. Open the desired firmware project:

   ```sh
   cd firmware/10-launcher   # or firmware/00-hello-world
   ```

   PlatformIO will download dependencies on the first build.

### Tools (Node.js)

Node.js >= 18 is required.

```sh
cd tools/provision      && npm install
cd tools/creds-watcher  && npm install
cd tools/claude-endpoint && npm install
```

---

## Building the Firmware

```sh
cd firmware/10-launcher

# Compile only (no upload)
pio run -e esp32dev

# Compile and flash
pio run -e esp32dev -t upload

# Upload filesystem image
pio run -t uploadfs
```

The build must pass with zero errors before a PR can be merged.

---

## Running Tests

### Firmware native tests

PlatformIO supports a `native` environment for unit-testing firmware logic
without hardware:

```sh
cd firmware/10-launcher
pio test -e native
```

Tests live in `firmware/10-launcher/test/`. New logic that can be isolated
from hardware should have corresponding native tests.

### Tool tests

```sh
cd tools/provision
node --test

cd tools/creds-watcher
node --test

cd tools/claude-endpoint
node --test
```

---

## Commit Message Convention

This project uses **Conventional Commits** style, matching the existing git
log:

```
<type>(<scope>): <short description>
```

Common types:

| Type | When to use |
|------|-------------|
| `feat` | New feature or capability |
| `fix` | Bug fix |
| `docs` | Documentation only |
| `refactor` | Code restructure with no behavior change |
| `style` | Formatting, whitespace — no logic change |
| `test` | Adding or updating tests |
| `chore` | Build system, dependency updates, tooling |
| `perf` | Performance improvement |

Scope examples: `10-launcher`, `tools/provision`, `shared`, `hardware`.

Examples from the project history:

```
feat(10-launcher): interactive screen mirror + redesigned web portal
fix(10-launcher): USE_HSPI_PORT so SD VSPI init doesn't break shared-bus touch
perf(10-launcher): run Claude usage poll off the UI thread
```

Keep the subject line under 72 characters. Use the body for "why", not "what".

---

## Pull Request Process

1. Fork the repository and create a feature branch from `main`:

   ```sh
   git checkout -b feat/my-feature
   ```

2. Make your changes. Ensure the firmware compiles cleanly (`pio run -e esp32dev`).
3. Add or update tests where appropriate.
4. Commit using the convention above.
5. Push and open a PR against `main`.
6. Fill in the PR template — include a description of what changed and why.
7. A maintainer will review. Feedback is addressed by pushing additional commits
   (do not force-push during review).
8. Once approved and CI is green, the PR will be squash-merged.

---

## Hardware Testing Note

Changes that touch any of the following **must be tested on real hardware**
before the PR is merged:

- Display driver configuration (`TFT_eSPI` build flags, pin map)
- Touch driver or calibration logic
- SPI bus initialisation order (especially anything that could affect
  `USE_HSPI_PORT` behavior)
- SD card mounting or filesystem access
- WiFi / captive-portal / SoftAP flows
- NVS read / write paths

If you do not have access to the hardware, note that clearly in the PR and a
maintainer with hardware will test it before merging.

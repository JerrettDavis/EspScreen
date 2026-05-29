# apps — MicroPython App Store (Phase 3+)

This directory will hold uploadable MicroPython applications for EspScreen. Each app lives in its own subdirectory with a manifest file, entry point, and any assets.

Apps are loaded from `/apps` on the LittleFS partition at runtime. The launcher scans for manifests, builds the tile grid, and launches the selected app's `main.py` in the MicroPython VM with an LVGL handle.

See [/PLAN.md](../PLAN.md) (ADR-002 and Phase 3) for the full app loader contract and manifest spec. Implementation starts in Phase 2.

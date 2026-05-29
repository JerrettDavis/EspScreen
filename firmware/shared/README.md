# shared — Cross-Firmware Headers

Single source of truth for hardware pin assignments and other constants shared across all firmware phases. All phase directories (`00-hello-world`, `10-launcher`, `20-slave-spi`) should reference `shared/pinmap.h` rather than define pins locally.

Primary file: [`pinmap.h`](pinmap.h) — every GPIO for TFT, touch, SD, and backlight.

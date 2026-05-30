## Summary

<!-- What does this PR change and why? -->

## Type of change

- [ ] Firmware (ESP32 application code)
- [ ] Build / PlatformIO config
- [ ] Node tool (`tools/*`)
- [ ] CI/CD (`.github/`)
- [ ] Docs / hardware

## Hardware tested

- **Board**: <!-- e.g. ESP32 DevKit v1, custom PCB rev X -->
- **Display/touch wired**: <!-- yes / no / N/A -->
- **Firmware version / commit**: <!-- SHA or version string -->

## Testing done

- [ ] `pio run -e esp32dev` passes locally
- [ ] Flashed and booted on device
- [ ] Serial log reviewed (no unexpected errors)
- [ ] Node tool tests pass (`node --test`) — if applicable
- [ ] Native unit tests pass (`pio test -e native`) — if applicable

## Serial log snippet (if relevant)

<details>
<summary>Boot log</summary>

```
(paste here)
```

</details>

## Checklist

- [ ] No COM port or machine-specific paths left in code
- [ ] Secrets / credentials not committed
- [ ] `platformio.ini` not modified for local-only settings

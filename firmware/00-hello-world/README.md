# 00-hello-world — Phase 0 Bring-up

Lights the ST7796 display, shows "EspScreen v0.0.1", and cycles backgrounds on tap. Serial logs touch coordinates.

## Build & Flash

Ensure PlatformIO core is on your PATH (`pio --version` should succeed).

```sh
cd firmware/00-hello-world

# Compile only
pio run

# Flash (auto-detects COM port)
pio run -t upload

# Serial monitor (Ctrl+C to exit)
pio device monitor
```

## Expected Behaviour

1. Screen lights up immediately (backlight GPIO 27 pulled HIGH in `setup()`).
2. Black background with white text: `EspScreen v0.0.1` centred, "Tap to cycle" below.
3. Each tap cycles: BLACK → RED → GREEN → BLUE → WHITE → BLACK …
4. Serial @ 115200 prints on each tap:
   ```
   [EspScreen v0.0.1] Phase 0 hello-world starting...
   Ready. Tap the screen to cycle background colors.
   touch x=142 y=231  color=RED (idx=1)
   touch x=155 y=240  color=GREEN (idx=2)
   ```

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| Screen stays black | Backlight not firing | Confirm GPIO 27 wired to BL; check USB power (500 mA minimum) |
| Colours wrong / inverted | RGB vs BGR order mismatch | Toggle `TFT_RGB_ORDER`: change `-DTFT_RGB_ORDER=TFT_BGR` to `TFT_RGB` in `platformio.ini`, rebuild |
| Touch coords nonsense | Placeholder calibration | Expected — Phase 1 adds 4-corner calibration. Verify orientation only for now |
| Upload fails "No serial port" | Wrong COM port | Confirm device on COM20 with `pio device list`; update `upload_port` in `platformio.ini` |
| Upload fails "Permission denied" | Port in use | Close Serial Monitor or other programs holding COM20 |
| Screen white/garbled after flash | Init issue | Try power-cycle (unplug USB), reflash |

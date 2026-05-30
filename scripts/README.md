# scripts — Development Helper Scripts

PowerShell scripts for serial capture and board reset during development.

> **Note:** These scripts hard-code **COM20**. Edit the `SerialPort("COM20", ...)`
> line in each script to match the COM port your board is assigned on your machine
> before running.

---

| Script | Purpose |
|--------|---------|
| `capture_boot.ps1` | Reset the board via DTR and capture 12 seconds of serial output |
| `capture_boot2.ps1` | Reset the board via RTS and capture 12 seconds of serial output |
| `capture_serial.ps1` | Send a `cal` command over serial and capture the calibration output |

---

## Usage

```powershell
# Edit COM port first (default is COM20)
# Then run from the repo root:
.\scripts\capture_boot.ps1
```

These scripts require no additional dependencies — they use
`System.IO.Ports.SerialPort` from the .NET base class library included with
Windows PowerShell and PowerShell 7.

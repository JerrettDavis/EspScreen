"""
Capture fresh boot log - try multiple reset methods for ESP32.
Standard ESP32 dev boards: DTR -> EN (reset), RTS -> GPIO0 (boot mode).
esptool hard_reset: RTS=False (EN goes HIGH = release reset).
"""
import serial
import serial.serialutil
import time
import sys

PORT = "COM20"
BAUD = 115200

print(f"Opening {PORT} at {BAUD}...")
try:
    ser = serial.Serial(PORT, BAUD, timeout=1, dsrdtr=False, rtscts=False)
except Exception as e:
    print(f"ERROR: {e}")
    sys.exit(1)

# Method: DTR controls EN (reset) on many ESP32 boards
# Pull EN low (DTR=True on inverted logic), then release
print("Resetting ESP32 via DTR (EN line)...")
ser.setDTR(False)
time.sleep(0.1)
ser.setDTR(True)   # DTR active = EN LOW = hold in reset
time.sleep(0.2)
ser.setDTR(False)  # DTR inactive = EN HIGH = release reset = boot
time.sleep(0.05)

print(f"\n=== BOOT LOG (capturing 14s after reset) ===")
deadline = time.time() + 14
boot_lines = []
while time.time() < deadline:
    if ser.in_waiting:
        try:
            line = ser.readline().decode(errors="replace").rstrip()
        except Exception:
            continue
        if line:
            boot_lines.append(line)
            print(line)
    else:
        time.sleep(0.05)

if not boot_lines:
    print("No output received. Trying RTS method...")
    # Some boards use RTS for reset
    ser.setRTS(True)
    time.sleep(0.2)
    ser.setRTS(False)
    deadline2 = time.time() + 10
    while time.time() < deadline2:
        if ser.in_waiting:
            try:
                line = ser.readline().decode(errors="replace").rstrip()
            except Exception:
                continue
            if line:
                boot_lines.append(line)
                print(line)
        else:
            time.sleep(0.05)

ser.close()
print(f"\n=== BOOT CAPTURE DONE ({len(boot_lines)} lines) ===")

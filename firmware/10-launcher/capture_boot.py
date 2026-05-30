"""
Capture fresh boot log by toggling RTS/DTR to reset ESP32.
"""
import serial
import time
import sys

PORT = "COM20"
BAUD = 115200

print(f"Opening {PORT} at {BAUD}...")
try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
except Exception as e:
    print(f"ERROR: {e}")
    sys.exit(1)

# Reset ESP32 via RTS (same method esptool uses for hard reset)
print("Resetting ESP32 via RTS...")
ser.setRTS(True)
time.sleep(0.1)
ser.setRTS(False)
time.sleep(0.1)

print(f"\n=== BOOT LOG (capturing 12s after reset) ===")
deadline = time.time() + 12
while time.time() < deadline:
    if ser.in_waiting:
        line = ser.readline().decode(errors="replace").rstrip()
        if line:
            print(line)
    else:
        time.sleep(0.05)

ser.close()
print("\n=== BOOT CAPTURE DONE ===")

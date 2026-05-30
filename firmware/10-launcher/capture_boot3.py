"""
Capture boot log - reset via DTR and immediately stream with no interruption.
"""
import serial
import time
import sys

PORT = "COM20"
BAUD = 115200

print(f"Opening {PORT}...")
try:
    ser = serial.Serial(PORT, BAUD, timeout=0.5, dsrdtr=False, rtscts=False, exclusive=True)
except Exception as e:
    print(f"ERROR: {e}")
    sys.exit(1)

# DTR reset
print("Reset via DTR...")
ser.dtr = True
time.sleep(0.3)
ser.dtr = False

print("=== BOOT LOG (30s) ===")
deadline = time.time() + 30
buf = b""
while time.time() < deadline:
    chunk = ser.read(256)
    if chunk:
        buf += chunk
        # Print complete lines
        while b'\n' in buf:
            idx = buf.index(b'\n')
            line = buf[:idx].decode(errors="replace").strip('\r\n ')
            buf = buf[idx+1:]
            if line:
                print(line)
    else:
        time.sleep(0.01)

ser.close()
print("=== DONE ===")

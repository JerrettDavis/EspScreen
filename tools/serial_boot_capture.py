"""
Trigger a reset and capture full boot log.
"""
import serial, time, sys

port = "COM20"
baud = 115200

try:
    s = serial.Serial(port, baud, timeout=0.5)
except Exception as e:
    print(f"[FAIL] Cannot open {port}: {e}")
    sys.exit(1)

# Send reset command
s.write(b"reset\r\n")
print(">>> reset (sent, watching for boot...)")
time.sleep(0.5)

# Capture for 12 seconds to get full boot sequence
start = time.time()
while time.time() - start < 12:
    line = s.readline()
    if line:
        decoded = line.decode('utf-8', errors='replace').rstrip()
        if decoded:
            print(decoded.encode('ascii', errors='replace').decode('ascii'), flush=True)

s.close()
print("--- capture done ---")

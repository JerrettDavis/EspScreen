import serial, time, sys

port = "COM20"
baud = 115200
duration = float(sys.argv[1]) if len(sys.argv) > 1 else 8.0
cmd = sys.argv[2] if len(sys.argv) > 2 else None

try:
    s = serial.Serial(port, baud, timeout=2)
except Exception as e:
    print(f"[FAIL] Cannot open {port}: {e}")
    sys.exit(1)

# Optionally send a command after a short delay
if cmd:
    time.sleep(0.5)
    s.write((cmd + "\r\n").encode())
    print(f"[SENT] {cmd}")

start = time.time()
while time.time() - start < duration:
    line = s.readline()
    if line:
        decoded = line.decode('utf-8', errors='replace').rstrip()
        print(decoded)

s.close()

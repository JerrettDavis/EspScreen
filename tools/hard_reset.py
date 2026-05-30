"""Force reset via DTR toggle and capture boot."""
import serial, time, sys

port = "COM20"
baud = 115200

try:
    s = serial.Serial(port, baud, timeout=0.5)
except Exception as e:
    print(f"[FAIL] {e}")
    sys.exit(1)

# Try serial reset command first
s.write(b"reset\r\n")
print("[sent] reset command")
time.sleep(0.5)

# If no boot, try DTR/RTS toggle (same as what esptool uses to reset)
s.dtr = False
s.rts = True
time.sleep(0.1)
s.dtr = True
s.rts = False
time.sleep(0.1)
s.dtr = False
print("[sent] DTR/RTS reset pulse")

# Capture boot
start = time.time()
while time.time() - start < 12:
    line = s.readline()
    if line:
        d = line.decode('utf-8', errors='replace').rstrip().encode('ascii', errors='replace').decode('ascii')
        if d:
            print(d, flush=True)

s.close()
print("--- done ---")

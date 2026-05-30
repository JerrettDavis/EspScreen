"""Proper ESP32 reset: EN pin via RTS, no DTR."""
import serial, time, sys

port = "COM20"
baud = 115200

try:
    s = serial.Serial(port, baud, timeout=0.5)
except Exception as e:
    print(f"[FAIL] {e}")
    sys.exit(1)

# ESP32 RESET = EN pin. On most dev boards, RTS is connected to EN.
# DTR=1, RTS=0 releases EN (normal run)
# DTR=1, RTS=1 pulls EN low (reset)
# Then DTR=1, RTS=0 to run
s.dtr = True
s.rts = False
time.sleep(0.1)
s.rts = True   # pull EN low
time.sleep(0.1)
s.rts = False  # release EN
print("[reset] EN pulse sent (RTS high->low)")
time.sleep(0.2)

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

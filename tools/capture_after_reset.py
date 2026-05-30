"""Capture boot output after a fresh esptool reset — wait for 'UI ready'."""
import serial, time, sys

port = "COM20"
baud = 115200

try:
    s = serial.Serial(port, baud, timeout=0.5)
except Exception as e:
    print(f"[FAIL] {e}")
    sys.exit(1)

start = time.time()
ui_ready = False
while time.time() - start < 15:
    line = s.readline()
    if line:
        d = line.decode('utf-8', errors='replace').rstrip().encode('ascii', errors='replace').decode('ascii')
        if d:
            print(d, flush=True)
            if 'UI ready' in d:
                ui_ready = True

if ui_ready:
    print("\n[OK] Device booted and UI is ready!")
else:
    # Maybe it already booted — try info
    s.write(b"info\r\n")
    time.sleep(2)
    while True:
        line = s.readline()
        if not line:
            break
        d = line.decode('utf-8', errors='replace').rstrip().encode('ascii', errors='replace').decode('ascii')
        if d:
            print(d)

s.close()
print("--- done ---")

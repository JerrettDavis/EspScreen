"""
Wait for boot output and send 'reset' quickly in the recovery window to factory reset.
The recovery window is 5s. We need to connect and send 'reset' within that window.
"""
import serial, time, sys

port = "COM20"
baud = 115200

try:
    s = serial.Serial(port, baud, timeout=0.2)
except Exception as e:
    print(f"[FAIL] {e}")
    sys.exit(1)

start = time.time()
in_recovery = False
reset_sent = False
lines = []

while time.time() - start < 20:
    line = s.readline()
    if line:
        d = line.decode('utf-8', errors='replace').rstrip().encode('ascii', errors='replace').decode('ascii')
        if d:
            lines.append(d)
            print(d, flush=True)
            if 'Recovery window' in d and not reset_sent:
                # In recovery window — send factory reset
                time.sleep(0.3)
                s.write(b"reset\r\n")
                print("[SENT FACTORY RESET]", flush=True)
                reset_sent = True
                in_recovery = True
            if 'UI ready' in d:
                print("[OK] Normal boot complete", flush=True)
                break

s.close()
print(f"--- done (reset_sent={reset_sent}) ---")

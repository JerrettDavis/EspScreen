"""
Enable mirror and capture screen dump output.
Also re-runs calculator test so we can see display value in mirror output.
"""
import serial, time, sys, threading

port = "COM20"
baud = 115200

try:
    s = serial.Serial(port, baud, timeout=0.1)
except Exception as e:
    print(f"[FAIL] {e}")
    sys.exit(1)

stop_flag = False
lines = []
def reader():
    while not stop_flag:
        line = s.readline()
        if line:
            d = line.decode('utf-8', errors='replace').rstrip().encode('ascii', errors='replace').decode('ascii')
            if d:
                lines.append(d)
                print(d, flush=True)

t = threading.Thread(target=reader, daemon=True)
t.start()

def send(cmd, delay=1.0):
    s.write((cmd + "\r\n").encode())
    print(f">>> {cmd}", flush=True)
    time.sleep(delay)

def tap(x, y, delay=0.8):
    send(f"touch {x} {y}", delay)

# Check if mirror works
send("mirror status", 1.5)
send("mirror on", 2.0)
send("mirror status", 1.5)

stop_flag = True
time.sleep(0.2)
s.close()

# Check if mirror output appeared
mirror_lines = [l for l in lines if 'mirror' in l.lower() or 'frame' in l.lower() or 'px' in l.lower()]
print(f"\n=== Mirror-related lines: {len(mirror_lines)} ===")
for l in mirror_lines:
    print(f"  {l}")

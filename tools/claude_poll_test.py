"""Open the Claude tile and trigger a poll, observe auth status messages."""
import serial, time, sys, threading

port = "COM20"
baud = 115200

try:
    s = serial.Serial(port, baud, timeout=0.1)
except Exception as e:
    print(f"[FAIL] {e}")
    sys.exit(1)

stop_flag = False
def reader():
    while not stop_flag:
        line = s.readline()
        if line:
            d = line.decode('utf-8', errors='replace').rstrip().encode('ascii', errors='replace').decode('ascii')
            if d:
                print(d, flush=True)

t = threading.Thread(target=reader, daemon=True)
t.start()

def tap(label, x, y, delay=1.5):
    s.write((f"touch {x} {y}\r\n").encode())
    print(f">>> [{label}] touch {x} {y}", flush=True)
    time.sleep(delay)

def send(cmd, delay=2.0):
    s.write((cmd + "\r\n").encode())
    print(f">>> {cmd}", flush=True)
    time.sleep(delay)

# Navigate to Claude tile
print("=== Opening Claude tile ===")
tap("claude", 225, 218, 3.0)

# Force poll
send("claude poll", 10.0)

# Get status
send("claude get", 2.0)
send("info", 1.5)

# Back home
tap("back", 30, 30, 1.5)

stop_flag = True
time.sleep(0.3)
s.close()
print("\n=== Done ===")

"""
Slow calculator test — longer delays between taps, enable touch debug to see what lands.
Also waits for a fresh mirror frame before each screenshot.
"""
import serial, time, sys, threading, urllib.request, struct, os

port = "COM20"
baud = 115200
SCREEN_URL = "http://10.0.0.88/api/screen"
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))

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

def tap(x, y, delay=1.5):
    s.write((f"touch {x} {y}\r\n").encode())
    print(f">>> touch {x} {y}", flush=True)
    time.sleep(delay)

def send(cmd, delay=1.0):
    s.write((cmd + "\r\n").encode())
    print(f">>> {cmd}", flush=True)
    time.sleep(delay)

def fetch_screen(filename):
    time.sleep(0.5)  # wait for LVGL to re-render
    try:
        req = urllib.request.urlopen(SCREEN_URL, timeout=5)
        data = req.read()
        path = os.path.join(TOOLS_DIR, filename)
        with open(path, 'wb') as f:
            f.write(data)
        h = __import__('hashlib').sha256(data).hexdigest()[:12]
        print(f"[screen] {filename}: {len(data)} bytes hash={h}")
        return data
    except Exception as e:
        print(f"[screen] Fetch failed: {e}")
        return None

C0, C1, C2, C3 = 39, 119, 199, 279
R0, R1, R2, R3, R4 = 162, 231, 300, 369, 438

# Enable touch debug to see what LVGL says about each tap
send("tdbg", 1.0)

# Make sure mirror is on
send("mirror on", 1.5)

# Navigate to calculator
print("\n=== Going home ===")
tap(30, 30, 2.0)
data0 = fetch_screen("home.bmp")

print("\n=== Opening Calculator ===")
tap(83, 330, 3.0)
data1 = fetch_screen("calc_opened.bmp")

print("\n--- Tapping 7 ---")
tap(C0, R1, 2.0)
data2 = fetch_screen("after_7.bmp")

print("\n--- Tapping + ---")
tap(C3, R3, 2.0)
data3 = fetch_screen("after_plus.bmp")

print("\n--- Tapping 8 ---")
tap(C1, R1, 2.0)
data4 = fetch_screen("after_8.bmp")

print("\n--- Tapping = ---")
tap(280, R4, 2.0)
data5 = fetch_screen("after_eq.bmp")

# Compare hashes
hashes = {}
for name, d in [("home", data0), ("calc_opened", data1), ("after_7", data2),
                ("after_plus", data3), ("after_8", data4), ("after_eq=", data5)]:
    if d:
        import hashlib
        hashes[name] = hashlib.sha256(d).hexdigest()[:12]
    else:
        hashes[name] = "MISSING"

print("\n=== Hash comparison (screens should differ as digits are typed) ===")
for k, v in hashes.items():
    print(f"  {k}: {v}")

# Check for unique hashes
unique = len(set(hashes.values()))
print(f"\n  Unique screen states: {unique} (expected >= 3 if calculator is working)")

send("tdbg", 0.5)  # turn off touch debug
tap(30, 30, 1.5)   # back home

stop_flag = True
time.sleep(0.3)
s.close()
print("\n=== Done ===")

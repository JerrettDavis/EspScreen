"""
Check current screen by navigating explicitly and capturing router logs.
Try multiple back presses then navigate to calculator fresh.
"""
import serial, time, sys, threading, urllib.request, struct, os, hashlib

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

def send(cmd, delay=1.0):
    s.write((cmd + "\r\n").encode())
    print(f">>> {cmd}", flush=True)
    time.sleep(delay)

def fetch_hash():
    time.sleep(0.4)
    try:
        data = urllib.request.urlopen(SCREEN_URL, timeout=5).read()
        return hashlib.sha256(data).hexdigest()[:12], data
    except Exception as e:
        return f"ERR:{e}", None

# Try to get to home screen via multiple back presses
print("=== Pressing back 3x to ensure home ===")
for i in range(3):
    tap("back", 30, 30, 1.0)

h, _ = fetch_hash()
print(f"[hash] After backs: {h}")

print("\n=== Opening calculator tile (83, 330) ===")
tap("calc tile", 83, 330, 2.5)
h1, d1 = fetch_hash()
print(f"[hash] After calc tap: {h1}")

if h1 != h:
    print("[OK] Screen changed! Calculator opened.")
    # Run the calculation
    C0, C1, C2, C3 = 39, 119, 199, 279
    R0, R1, R2, R3, R4 = 162, 231, 300, 369, 438

    print("\n--- 7 + 8 = ---")
    tap("7", C0, R1, 1.0)
    h2, _ = fetch_hash()
    tap("+", C3, R3, 1.0)
    h3, _ = fetch_hash()
    tap("8", C1, R1, 1.0)
    h4, _ = fetch_hash()
    tap("=", 280, R4, 1.5)
    h5, d5 = fetch_hash()

    print(f"\n[hashes] 7:{h2} +:{h3} 8:{h4} =:{h5}")
    print(f"[unique] {len(set([h1,h2,h3,h4,h5]))} unique frames out of 5")
    if d5:
        with open(os.path.join(TOOLS_DIR, "calc_final.bmp"), 'wb') as f:
            f.write(d5)

    print("\n--- C 5 / 0 = ---")
    tap("C", C0, R0, 1.0)
    tap("5", C1, R2, 1.0)
    tap("/", C3, R0, 1.0)
    tap("0", 80, R4, 1.0)
    tap("=", 280, R4, 1.5)
    h_err, d_err = fetch_hash()
    print(f"[hash after 5/0=]: {h_err}")
    if d_err:
        with open(os.path.join(TOOLS_DIR, "calc_div0_final.bmp"), 'wb') as f:
            f.write(d_err)
else:
    print(f"[!] Screen did NOT change (home hash={h}, after tap hash={h1})")
    print("[!] Calculator tile tap may have missed — tile may not be at y=330")
    # Try different y positions for the calculator
    for trial_y in [310, 320, 340, 350]:
        tap(f"calc-y{trial_y}", 83, trial_y, 2.0)
        ht, _ = fetch_hash()
        print(f"  tap y={trial_y}: hash={ht} {'CHANGED!' if ht != h else 'same'}")
        if ht != h:
            break

send("mirror off", 0.5)

stop_flag = True
time.sleep(0.2)
s.close()
print("\n=== Done ===")

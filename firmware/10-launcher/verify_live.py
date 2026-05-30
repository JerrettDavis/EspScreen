"""
Live verification - no reset, just open port and interact.
The device is already running (flashed and booted).
"""
import serial
import time
import sys

PORT = "COM20"
BAUD = 115200

def read_for(ser, seconds):
    lines = []
    deadline = time.time() + seconds
    while time.time() < deadline:
        if ser.in_waiting:
            try:
                raw = ser.read(ser.in_waiting)
                text = raw.decode(errors="replace")
                for part in text.split('\n'):
                    part = part.strip('\r\n ')
                    if part:
                        lines.append(part)
                        print(part)
            except Exception as e:
                print(f"[read err] {e}")
        else:
            time.sleep(0.02)
    return lines

def send_cmd(ser, cmd, wait=2.5):
    print(f"\n>>> {cmd}")
    ser.write((cmd + "\r\n").encode())
    return read_for(ser, wait)

print(f"Opening {PORT} at {BAUD} (no reset)...")
try:
    # dsrdtr=False, rtscts=False = don't touch control lines
    ser = serial.Serial(
        PORT, BAUD, timeout=0.5,
        dsrdtr=False, rtscts=False,
        exclusive=True
    )
except Exception as e:
    print(f"ERROR: {e}")
    sys.exit(1)

# Don't touch DTR/RTS
print("Port open. Reading any pending output...")
read_for(ser, 1.0)

print("\n=== HEAP BASELINE (info) ===")
r_info1 = send_cmd(ser, "info", wait=2.0)
heap_before = next((l for l in r_info1 if "Free heap" in l), None)
uptime = next((l for l in r_info1 if "Uptime" in l), None)
print(f"\n  heap_before={heap_before}  uptime={uptime}")

# --- Calculator tile navigation ---
# From first run we know (80,280) triggered router push. Let's target that.
print("\n=== CALCULATOR NAVIGATION ===")
print("  [Tapping known-working position (80,280)]")
r_calc = send_cmd(ser, "touch 80 280", wait=3.0)
router_evts = [l for l in r_calc if "[router]" in l]
print(f"  Router events: {router_evts}")

print("\n  [Back (30,30)]")
r_back = send_cmd(ser, "touch 30 30", wait=2.5)
back_evts = [l for l in r_back if "[router]" in l]
print(f"  Back router events: {back_evts}")

# --- 10 nav transitions ---
print("\n=== 10 NAV TRANSITIONS ===")
nav_cmds = [
    ("touch 80 280",  "open row1,col0"),
    ("touch 30 30",   "back"),
    ("touch 240 280", "open row1,col1"),
    ("touch 30 30",   "back"),
    ("touch 80 160",  "open row0,col0"),
    ("touch 30 30",   "back"),
    ("touch 240 160", "open row0,col1"),
    ("touch 30 30",   "back"),
    ("touch 80 280",  "open row1,col0 again"),
    ("touch 30 30",   "back"),
]
all_nav = []
heap_samples = []
for cmd, label in nav_cmds:
    print(f"\n  [{label}] {cmd}")
    r = send_cmd(ser, cmd, wait=2.0)
    all_nav.extend(r)

router_all = [l for l in all_nav if "[router]" in l]
dou_all    = [l for l in all_nav if "delete_on_unload" in l]
print(f"\n  Total router events during 10 nav: {len(router_all)}")
for l in router_all: print(f"    {l}")
print(f"  delete_on_unload: {len(dou_all)}")
for l in dou_all: print(f"    {l}")

# --- Post-nav heap ---
print("\n=== POST-NAV HEAP ===")
r_info2 = send_cmd(ser, "info", wait=2.0)
heap_after = next((l for l in r_info2 if "Free heap" in l), None)
uptime2 = next((l for l in r_info2 if "Uptime" in l), None)
print(f"  heap_after={heap_after}  uptime2={uptime2}")

# --- Status commands ---
print("\n=== wifi status ===")
r_wifi = send_cmd(ser, "wifi status", wait=3.0)

print("\n=== net status ===")
r_net = send_cmd(ser, "net status", wait=3.0)

print("\n=== claude profile list ===")
r_claude = send_cmd(ser, "claude profile list", wait=2.0)

ser.close()

# --- Heap leak check ---
print("\n\n======== FINAL SUMMARY ========")
print(f"  Pre-nav heap:   {heap_before or 'N/A'}")
print(f"  Post-nav heap:  {heap_after or 'N/A'}")

# Extract numeric heap values
def extract_heap(line):
    if not line: return None
    try:
        # "Free heap: 69672" format
        parts = line.split()
        for i, p in enumerate(parts):
            if p.isdigit() or (p.replace(',','').isdigit()):
                return int(p.replace(',',''))
    except: pass
    return None

h1 = extract_heap(heap_before)
h2 = extract_heap(heap_after)
if h1 and h2:
    diff = h1 - h2
    print(f"  Heap delta:     {diff} bytes ({'LEAK possible' if diff > 2000 else 'STABLE (within noise)'})")

print(f"  Router events:  {len(router_all)}")
print(f"  delete_on_unload: {len(dou_all)}")
print(f"  WiFi:  {r_wifi}")
print(f"  Net:   {r_net}")
print(f"  Claude: {r_claude}")

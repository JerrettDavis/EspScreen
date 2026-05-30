"""
Full hardware verification: reset, capture boot, then run commands.
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
                line = ser.readline().decode(errors="replace").rstrip()
            except Exception:
                continue
            if line:
                lines.append(line)
                print(line)
        else:
            time.sleep(0.05)
    return lines

def send_cmd(ser, cmd, wait=2.0):
    print(f"\n>>> {cmd}")
    ser.write((cmd + "\r\n").encode())
    return read_for(ser, wait)

print(f"Opening {PORT} at {BAUD}...")
try:
    ser = serial.Serial(PORT, BAUD, timeout=1, dsrdtr=False, rtscts=False)
except Exception as e:
    print(f"ERROR: {e}")
    sys.exit(1)

# Reset via DTR
print("Resetting ESP32 via DTR...")
ser.setDTR(False)
time.sleep(0.1)
ser.setDTR(True)
time.sleep(0.2)
ser.setDTR(False)

print("\n=== BOOT LOG ===")
boot_lines = read_for(ser, 15)

print("\n--- Boot analysis ---")
heap_ready = next((l for l in boot_lines if "UI ready" in l), None)
print(f"UI ready line: {heap_ready or 'NOT FOUND'}")
crashes = [l for l in boot_lines if any(k in l for k in ["panic", "Backtrace", "Guru Meditation", "abort", "rst:0x"])]
print(f"Crash indicators: {crashes or 'NONE'}")

# --- Heap baseline ---
print("\n=== HEAP BASELINE ===")
r1 = send_cmd(ser, "info", wait=1.5)
heap_before = next((l for l in r1 if "Free heap" in l), "not found")
print(f"  Heap before nav: {heap_before}")

# --- Identify tile positions by checking what's on screen ---
# Home launcher tile layout for ESPScreen: typically 2-column grid
# First row starts around y=120-160 given header ~80px
# Try to navigate to Calculator - it's usually the first or second tile
# Tile positions (center): col0=80, col1=240; row0=160, row1=280, row2=400
print("\n=== NAVIGATOR: Try Calculator tile positions ===")
# Try each position and watch for router push events
tile_positions = [
    (80,  160, "row0,col0"),
    (240, 160, "row0,col1"),
    (80,  280, "row1,col0"),
    (240, 280, "row1,col1"),
]

router_events_all = []
for x, y, label in tile_positions[:2]:  # try first 2 tiles
    print(f"\n  Tapping {label} ({x},{y})")
    r = send_cmd(ser, f"touch {x} {y}", wait=2.5)
    router_evts = [l for l in r if "[router]" in l]
    router_events_all.extend(router_evts)
    if router_evts:
        print(f"  -> ROUTER EVENTS: {router_evts}")
        # go back
        print(f"  -> Going back")
        rb = send_cmd(ser, "touch 30 30", wait=2.0)
        router_events_all.extend([l for l in rb if "[router]" in l])
    else:
        print(f"  -> No router events (tile may be empty/non-navigable)")

# --- 10 navigation transitions ---
print("\n=== 10 NAVIGATION TRANSITIONS ===")
nav_sequence = [
    ("touch 80 160",  "tap tile row0,col0"),
    ("touch 30 30",   "back"),
    ("touch 240 160", "tap tile row0,col1"),
    ("touch 30 30",   "back"),
    ("touch 80 280",  "tap tile row1,col0"),
    ("touch 30 30",   "back"),
    ("touch 240 280", "tap tile row1,col1"),
    ("touch 30 30",   "back"),
    ("touch 80 160",  "tap tile row0,col0"),
    ("touch 30 30",   "back"),
]
all_nav_lines = []
delete_on_unload_count = 0
for cmd, label in nav_sequence:
    print(f"\n  [{label}]")
    r = send_cmd(ser, cmd, wait=2.0)
    all_nav_lines.extend(r)
    dou = [l for l in r if "delete_on_unload" in l]
    delete_on_unload_count += len(dou)

print(f"\n  delete_on_unload events seen: {delete_on_unload_count}")

# --- Post-nav heap ---
print("\n=== HEAP POST-NAVIGATION ===")
r2 = send_cmd(ser, "info", wait=1.5)
heap_after = next((l for l in r2 if "Free heap" in l), "not found")
print(f"  Heap after nav: {heap_after}")

# --- wifi status ---
print("\n=== wifi status ===")
send_cmd(ser, "wifi status", wait=2.0)

# --- net status ---
print("\n=== net status ===")
send_cmd(ser, "net status", wait=2.0)

# --- claude profile list ---
print("\n=== claude profile list ===")
send_cmd(ser, "claude profile list", wait=2.0)

ser.close()
print("\n=== VERIFICATION COMPLETE ===")
print(f"  Boot heap: {heap_ready}")
print(f"  Pre-nav:   {heap_before}")
print(f"  Post-nav:  {heap_after}")
print(f"  Router push/pop events seen: {len(router_events_all)}")
print(f"  delete_on_unload events: {delete_on_unload_count}")
print(f"  Crashes: {crashes or 'NONE'}")

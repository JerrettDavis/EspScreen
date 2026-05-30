"""
Hardware verification script for ESPScreen firmware.
Opens COM20 at 115200, captures boot log, then sends commands.
"""
import serial
import time
import sys

PORT = "COM20"
BAUD = 115200
BOOT_WAIT = 8  # seconds to capture boot log

def send_cmd(ser, cmd, wait=2.0):
    """Send a command and collect response lines."""
    ser.write((cmd + "\r\n").encode())
    time.sleep(wait)
    lines = []
    while ser.in_waiting:
        line = ser.readline().decode(errors="replace").rstrip()
        if line:
            lines.append(line)
    return lines

def read_available(ser, wait=1.0):
    """Read whatever is available after waiting."""
    time.sleep(wait)
    lines = []
    while ser.in_waiting:
        line = ser.readline().decode(errors="replace").rstrip()
        if line:
            lines.append(line)
    return lines

print(f"Opening {PORT} at {BAUD}...")
try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
except Exception as e:
    print(f"ERROR opening port: {e}")
    sys.exit(1)

# --- Capture boot log ---
print(f"\n=== BOOT LOG (capturing {BOOT_WAIT}s) ===")
boot_lines = []
deadline = time.time() + BOOT_WAIT
while time.time() < deadline:
    if ser.in_waiting:
        line = ser.readline().decode(errors="replace").rstrip()
        if line:
            boot_lines.append(line)
            print(line)
    else:
        time.sleep(0.05)

# Extract heap at UI ready
heap_at_ready = None
for l in boot_lines:
    if "UI ready" in l and "heap" in l:
        heap_at_ready = l

print(f"\n--- Boot heap line: {heap_at_ready or 'NOT FOUND'} ---")

# Check for panic/backtrace
panic_lines = [l for l in boot_lines if any(k in l for k in ["panic", "Backtrace", "Guru Meditation", "boot loop", "rst:"])]
print(f"--- Panic/crash indicators: {panic_lines or 'NONE'} ---")

# --- info command (heap baseline) ---
print("\n=== CMD: info ===")
r = send_cmd(ser, "info", wait=1.5)
for l in r: print(l)
heap_lines = [l for l in r if "heap" in l.lower() or "Free" in l]

# --- Navigate to Calculator (tap the calculator tile) ---
# Launcher grid: 320x480 screen. Tiles are in a grid.
# Based on typical launcher layout: tiles start around y=80 after header.
# Grid is likely 2 columns, each tile ~120px wide, ~100px tall.
# Calculator is typically tile index 0 or 1 — try tapping center of first tile.
print("\n=== TAP: Calculator tile (tap ~80,160 = col0, row0 tile center) ===")
r = send_cmd(ser, "touch 80 160", wait=2.5)
for l in r: print(l)

# Read any pending output
extra = read_available(ser, 1.5)
for l in extra: print(l)

# Check if we got router push
router_push = [l for l in r+extra if "[router]" in l]
print(f"--- Router events: {router_push or 'none yet'} ---")

# --- Go back ---
print("\n=== TAP: Back button (~30,30) ===")
r = send_cmd(ser, "touch 30 30", wait=2.0)
for l in r: print(l)
extra = read_available(ser, 1.0)
for l in extra: print(l)

# --- Do 10 navigation transitions ---
print("\n=== 10 NAVIGATION TRANSITIONS ===")
heap_readings = []
tiles = [
    ("touch 80 160", "tile col0,row0"),
    ("touch 30 30",  "back"),
    ("touch 240 160","tile col1,row0"),
    ("touch 30 30",  "back"),
    ("touch 80 160", "tile col0,row0"),
    ("touch 30 30",  "back"),
    ("touch 240 160","tile col1,row0"),
    ("touch 30 30",  "back"),
    ("touch 80 280", "tile col0,row1"),
    ("touch 30 30",  "back"),
]

for cmd, label in tiles:
    print(f"\n  [{label}] {cmd}")
    r = send_cmd(ser, cmd, wait=2.0)
    for l in r: print(f"    {l}")
    extra = read_available(ser, 0.8)
    for l in extra: print(f"    {l}")

# Heap check after navigation
print("\n=== CMD: info (post-navigation heap) ===")
r = send_cmd(ser, "info", wait=1.5)
for l in r: print(l)

# --- wifi status ---
print("\n=== CMD: wifi status ===")
r = send_cmd(ser, "wifi status", wait=2.0)
for l in r: print(l)

# --- net status ---
print("\n=== CMD: net status ===")
r = send_cmd(ser, "net status", wait=2.0)
for l in r: print(l)

# --- claude profile list ---
print("\n=== CMD: claude profile list ===")
r = send_cmd(ser, "claude profile list", wait=2.0)
for l in r: print(l)

ser.close()
print("\n=== DONE ===")

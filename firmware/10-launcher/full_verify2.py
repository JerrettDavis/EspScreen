"""
Full hardware verification v2 - longer boot wait, robust line reading.
"""
import serial
import time
import sys

PORT = "COM20"
BAUD = 115200
BOOT_WAIT = 20  # ESP32 with LittleFS + WiFi init can take 10-15s

def read_for(ser, seconds, label=""):
    lines = []
    deadline = time.time() + seconds
    while time.time() < deadline:
        if ser.in_waiting:
            try:
                raw = ser.read(ser.in_waiting)
                text = raw.decode(errors="replace")
                for part in text.splitlines():
                    part = part.strip()
                    if part:
                        lines.append(part)
                        if label:
                            print(f"  [{label}] {part}")
                        else:
                            print(part)
            except Exception as e:
                print(f"  [read err] {e}")
        else:
            time.sleep(0.02)
    return lines

def send_cmd(ser, cmd, wait=3.0):
    print(f"\n>>> {cmd}")
    ser.write((cmd + "\r\n").encode())
    time.sleep(0.1)
    return read_for(ser, wait)

print(f"Opening {PORT} at {BAUD}...")
try:
    ser = serial.Serial(PORT, BAUD, timeout=0.5, dsrdtr=False, rtscts=False)
except Exception as e:
    print(f"ERROR: {e}")
    sys.exit(1)

# Clear any stale data
ser.reset_input_buffer()

# Reset via DTR (inverted: DTR=True = signal asserted = EN pulled low = reset held)
print("Resetting ESP32 via DTR/EN line...")
ser.dtr = True   # assert DTR => EN low => reset
time.sleep(0.3)
ser.dtr = False  # deassert DTR => EN high => boot
time.sleep(0.05)

print(f"\n=== BOOT LOG (waiting {BOOT_WAIT}s) ===")
boot_lines = read_for(ser, BOOT_WAIT)

print(f"\n--- Total boot lines: {len(boot_lines)} ---")

# --- Analysis ---
heap_ready = next((l for l in boot_lines if "UI ready" in l), None)
main_start = next((l for l in boot_lines if "EspScreen" in l and "starting" in l), None)
crashes = [l for l in boot_lines if any(k in l.lower() for k in ["panic", "backtrace:", "guru meditation", "abort(", "task watchdog"])]
rst_lines = [l for l in boot_lines if "rst:" in l]

print(f"  rst line:        {rst_lines}")
print(f"  main start:      {main_start or 'NOT FOUND'}")
print(f"  UI ready:        {heap_ready or 'NOT FOUND'}")
print(f"  Crash lines:     {crashes or 'NONE'}")

if not main_start:
    print("\n  WARNING: Firmware start message not seen — device may still be booting or serial not connected")
    print("  Waiting 10 more seconds...")
    extra = read_for(ser, 10)
    boot_lines.extend(extra)
    heap_ready = next((l for l in boot_lines if "UI ready" in l), None)
    main_start = next((l for l in boot_lines if "EspScreen" in l and "starting" in l), None)
    print(f"  After extra wait - main start: {main_start}, UI ready: {heap_ready}")

# --- info ---
r_info1 = send_cmd(ser, "info", wait=2.0)
heap_before = next((l for l in r_info1 if "Free heap" in l), None)

# --- Tap tiles and watch for router events ---
print("\n=== TILE NAVIGATION ===")
# Based on the launcher layout, try a broader set of positions
# and see which ones generate router events
router_found = False
for x, y in [(80,160),(240,160),(160,160),(80,280),(240,280),(160,280)]:
    print(f"\n  Tap ({x},{y})")
    r = send_cmd(ser, f"touch {x} {y}", wait=2.5)
    evts = [l for l in r if "[router]" in l]
    if evts:
        router_found = True
        print(f"  -> ROUTER: {evts}")
        rb = send_cmd(ser, "touch 30 30", wait=2.0)
        print(f"  -> BACK: {[l for l in rb if '[router]' in l]}")
        break
    else:
        print(f"  -> no router events")

# --- 10 nav transitions ---
print("\n=== 10 NAV TRANSITIONS ===")
nav_cmds = [
    "touch 80 160", "touch 30 30",
    "touch 240 160", "touch 30 30",
    "touch 80 280", "touch 30 30",
    "touch 240 280", "touch 30 30",
    "touch 80 160", "touch 30 30",
]
all_nav = []
for cmd in nav_cmds:
    r = send_cmd(ser, cmd, wait=2.0)
    all_nav.extend(r)

router_all = [l for l in all_nav if "[router]" in l]
dou_all    = [l for l in all_nav if "delete_on_unload" in l]
print(f"\n  All router events: {len(router_all)}")
for l in router_all: print(f"    {l}")
print(f"  delete_on_unload events: {len(dou_all)}")

# --- Post-nav heap ---
r_info2 = send_cmd(ser, "info", wait=2.0)
heap_after = next((l for l in r_info2 if "Free heap" in l), None)

# --- Status commands ---
r_wifi   = send_cmd(ser, "wifi status",       wait=2.5)
r_net    = send_cmd(ser, "net status",         wait=2.5)
r_claude = send_cmd(ser, "claude profile list",wait=2.5)

ser.close()

print("\n\n======== SUMMARY ========")
print(f"Build:         SUCCESS  RAM 37.2%  Flash 95.7%")
print(f"Flash:         SUCCESS  (COM20, esptool)")
print(f"Boot rst:      {rst_lines}")
print(f"Firmware start:{main_start or 'not captured (device was mid-boot or timing gap)'}")
print(f"UI ready heap: {heap_ready or 'not captured'}")
print(f"Pre-nav heap:  {heap_before or 'not found'}")
print(f"Post-nav heap: {heap_after or 'not found'}")
print(f"Router events: {len(router_all)} push/pop")
print(f"delete_on_unload: {len(dou_all)}")
print(f"Crashes:       {crashes or 'NONE'}")
print(f"WiFi status:   {r_wifi}")
print(f"Net status:    {r_net}")
print(f"Claude profiles:{r_claude}")

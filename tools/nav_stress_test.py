"""
Navigation stress test — exercises all 5 tiles repeatedly, captures heap readings
and looks for delete_on_unload log lines.
"""
import serial, time, sys, threading

port = "COM20"
baud = 115200

# Tile coords (center of each tile on home screen)
# Grid: 300px wide, left edge at x=10, top edge at y=48
# Tiles: 130x100, pad=8, gap=12
# Row 1: y_center = 48+8+50 = 106
# Row 2: y_center = 48+8+100+12+50 = 218
# Row 3: y_center = 48+8+200+24+50 = 330
# Col 1: x_center = 10+8+65 = 83
# Col 2: x_center = 10+8+130+12+65 = 225
TILES = {
    "Settings":    (83,  106),
    "TouchTest":   (225, 106),
    "About":       (83,  218),
    "Claude":      (225, 218),
    "Calculator":  (83,  330),
}
BACK = (30, 30)  # top-left back button

try:
    s = serial.Serial(port, baud, timeout=0.1)
except Exception as e:
    print(f"[FAIL] {e}")
    sys.exit(1)

output_lines = []
stop_flag = False

def reader():
    while not stop_flag:
        line = s.readline()
        if line:
            decoded = line.decode('utf-8', errors='replace').rstrip()
            decoded_ascii = decoded.encode('ascii', errors='replace').decode('ascii')
            if decoded_ascii:
                output_lines.append(decoded_ascii)
                print(decoded_ascii, flush=True)

t = threading.Thread(target=reader, daemon=True)
t.start()

def send(cmd, delay=0.8):
    s.write((cmd + "\r\n").encode())
    print(f">>> {cmd}", flush=True)
    time.sleep(delay)

def tap(x, y, delay=1.2):
    send(f"touch {x} {y}", delay)

def info():
    send("info", 1.5)

# Baseline
print("=== BASELINE ===")
info()

# Navigation cycles
transition_count = 0
cycle_order = ["Settings", "TouchTest", "About", "Calculator", "Claude", "TouchTest", "Settings", "About"]

print("\n=== NAVIGATION STRESS TEST ===")
for cycle in range(3):  # 3 full cycles = 24 transitions
    print(f"\n--- Cycle {cycle+1} ---")
    for app_name in cycle_order:
        x, y = TILES[app_name]
        print(f"\n[NAV] Opening: {app_name}")
        tap(x, y, 1.5)  # tap tile
        transition_count += 1
        time.sleep(0.3)
        tap(BACK[0], BACK[1], 1.5)  # tap back
        transition_count += 1
        time.sleep(0.3)
        # Check heap every 4 transitions
        if transition_count % 4 == 0:
            print(f"\n[HEAP CHECK @ {transition_count} transitions]")
            info()

print(f"\n=== FINAL HEAP CHECK (after {transition_count} transitions) ===")
info()

# Count delete_on_unload lines
delete_lines = [l for l in output_lines if "delete_on_unload" in l]
print(f"\n=== SUMMARY ===")
print(f"Total transitions: {transition_count}")
print(f"delete_on_unload events seen: {len(delete_lines)}")
for dl in delete_lines[:10]:
    print(f"  {dl}")

stop_flag = True
time.sleep(0.2)
s.close()

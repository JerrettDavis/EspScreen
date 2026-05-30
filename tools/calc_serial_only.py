"""
Calculator test via serial only — no HTTP screenshots.
Navigate to calc, observe router log for screen push/pop,
then check that back navigation still frees screens correctly.
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
all_lines = []
def reader():
    while not stop_flag:
        line = s.readline()
        if line:
            d = line.decode('utf-8', errors='replace').rstrip().encode('ascii', errors='replace').decode('ascii')
            if d:
                all_lines.append(d)
                print(d, flush=True)

t = threading.Thread(target=reader, daemon=True)
t.start()

def tap(label, x, y, delay=1.0):
    s.write((f"touch {x} {y}\r\n").encode())
    print(f">>> [{label}] touch {x} {y}", flush=True)
    time.sleep(delay)

def send(cmd, delay=1.0):
    s.write((cmd + "\r\n").encode())
    print(f">>> {cmd}", flush=True)
    time.sleep(delay)

# Button positions
C0, C1, C2, C3 = 39, 119, 199, 279
R0, R1, R2, R3, R4 = 162, 231, 300, 369, 438

print("=== Baseline info ===")
send("info", 2.0)

print("\n=== Opening Calculator (should see [router] push) ===")
tap("calc-tile", 83, 330, 2.0)

print("\n=== Test 7+8= via taps ===")
tap("7", C0, R1, 0.7)
tap("+", C3, R3, 0.7)
tap("8", C1, R1, 0.7)
tap("=", 280, R4, 1.5)

print("\n=== Test C 5/0= ===")
tap("C", C0, R0, 0.7)
tap("5", C1, R2, 0.7)
tap("/", C3, R0, 0.7)
tap("0", 80, R4, 0.7)
tap("=", 280, R4, 1.5)

send("info", 1.5)

print("\n=== Back to home (should see delete_on_unload) ===")
tap("back", 30, 30, 2.0)

send("info", 2.0)

print("\n=== Navigate Settings and back ===")
tap("settings", 83, 106, 2.0)
tap("back", 30, 30, 2.0)
send("info", 2.0)

print("\n=== Navigate About and back ===")
tap("about", 83, 218, 2.0)
tap("back", 30, 30, 2.0)
send("info", 2.0)

stop_flag = True
time.sleep(0.3)
s.close()

print("\n=== Summary ===")
router_pushes = [l for l in all_lines if '[router] push' in l]
router_pops   = [l for l in all_lines if '[router] pop' in l]
delete_lines  = [l for l in all_lines if 'delete_on_unload' in l]
print(f"router push events: {len(router_pushes)}")
print(f"router pop events:  {len(router_pops)}")
print(f"delete_on_unload:   {len(delete_lines)}")
for d in delete_lines:
    print(f"  {d}")
print("\n[Done]")

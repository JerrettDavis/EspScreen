"""
Run calculator test with mirror enabled — capture frame dump to identify result.
The mirror outputs ASCII art of the screen in 80x120 resolution.
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

def tap(x, y, delay=0.7):
    s.write((f"touch {x} {y}\r\n").encode())
    print(f">>> touch {x} {y}", flush=True)
    time.sleep(delay)

def send(cmd, delay=0.8):
    s.write((cmd + "\r\n").encode())
    print(f">>> {cmd}", flush=True)
    time.sleep(delay)

C0, C1, C2, C3 = 39, 119, 199, 279
R0, R1, R2, R3, R4 = 162, 231, 300, 369, 438

# Navigate to calculator
print("=== Go home then open calculator ===")
tap(30, 30, 1.5)
tap(83, 330, 2.0)

# wait for mirror frame
time.sleep(1.5)
print("\n=== FRAME after opening calculator ===")
# Trigger a manual mirror refresh by sending mirror command
send("mirror status", 0.5)

# Test 1: 7 + 8 =
print("\n--- 7 + 8 = ---")
tap(C0, R1, 0.5)   # 7
tap(C3, R3, 0.5)   # +
tap(C1, R1, 0.5)   # 8
tap(280, R4, 1.5)  # =

print("\n=== Frame after 7+8= ===")
time.sleep(2.0)

# Test 2: C 5 / 0 =
print("\n--- C 5 / 0 = ---")
tap(C0, R0, 0.5)   # C
tap(C1, R2, 0.5)   # 5
tap(C3, R0, 0.5)   # /
tap(80, R4, 0.5)   # 0
tap(280, R4, 1.5)  # =

print("\n=== Frame after 5/0= ===")
time.sleep(2.0)

# Turn off mirror and go back
send("mirror off", 0.5)
tap(30, 30, 1.5)

stop_flag = True
time.sleep(0.2)
s.close()

print("\n=== All captured lines ===")
# Print the last 60 lines (most interesting)
for l in lines[-60:]:
    print(l)

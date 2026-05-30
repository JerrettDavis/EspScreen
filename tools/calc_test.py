"""
Calculator functional test.
Navigates to calculator and performs: 7 + 8 = (expect 15), then 5 / 0 = (expect Error)
Calculator tile: touch 83 330
Back: touch 30 30

Calculator layout (from source): 4-column grid below display area.
Screen is 320x480. Need to find button positions from source.
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
def reader():
    while not stop_flag:
        line = s.readline()
        if line:
            d = line.decode('utf-8', errors='replace').rstrip().encode('ascii', errors='replace').decode('ascii')
            if d:
                print(d, flush=True)

t = threading.Thread(target=reader, daemon=True)
t.start()

def send(cmd, delay=1.0):
    s.write((cmd + "\r\n").encode())
    print(f">>> {cmd}", flush=True)
    time.sleep(delay)

def tap(x, y, delay=0.8):
    send(f"touch {x} {y}", delay)

# Navigate to calculator
print("=== Opening Calculator ===")
tap(83, 330, 2.0)

# Mirror on to see the screen
print("=== Enabling mirror to see screen state ===")
send("mirror on", 2.0)

stop_flag = True
time.sleep(0.2)
s.close()

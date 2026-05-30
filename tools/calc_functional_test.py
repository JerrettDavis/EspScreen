"""
Calculator functional test.
Layout from calculator_stub.cpp:
  BTN_Y=128, BTN_H=352, 5 rows, 2px gap between buttons
  Row centers (approx): 162, 231, 300, 369, 438
  Col centers (4-col, 320px wide, 2px gaps, ~79px per col): 39, 119, 199, 279

  Row 0:  C(0,162)   ±(1,162)  %(2,162)  ÷(3,162)
  Row 1:  7(0,231)   8(1,231)  9(2,231)  ×(3,231)
  Row 2:  4(0,300)   5(1,300)  6(2,300)  −(3,300)
  Row 3:  1(0,369)   2(1,369)  3(2,369)  +(3,369)
  Row 4:  0-wide(80,438)        .(200,438)  =(280,438)

Test 1: 7 + 8 = → expect 15
Test 2: 5 / 0 = → expect Error
"""
import serial, time, sys, threading

port = "COM20"
baud = 115200

# Col centers
C0, C1, C2, C3 = 39, 119, 199, 279
# Row centers
R0, R1, R2, R3, R4 = 162, 231, 300, 369, 438

BTNS = {
    'C':  (C0, R0), 'pm': (C1, R0), '%':  (C2, R0), '/':  (C3, R0),
    '7':  (C0, R1), '8':  (C1, R1), '9':  (C2, R1), '*':  (C3, R1),
    '4':  (C0, R2), '5':  (C1, R2), '6':  (C2, R2), '-':  (C3, R2),
    '1':  (C0, R3), '2':  (C1, R3), '3':  (C2, R3), '+':  (C3, R3),
    '0':  (80, R4), '.':  (200, R4),'=':  (280, R4),
}

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

def tap(x, y, delay=0.6):
    cmd = f"touch {x} {y}"
    s.write((cmd + "\r\n").encode())
    print(f">>> {cmd}", flush=True)
    time.sleep(delay)

# Make sure we're on home screen first (back out of wherever we are)
print("=== Tapping back to ensure home screen ===")
tap(30, 30, 1.5)

print("\n=== Opening Calculator ===")
tap(83, 330, 2.0)  # Calculator tile

print("\n--- Test 1: 7 + 8 = (expect 15) ---")
tap(*BTNS['7'], 0.5)
tap(*BTNS['+'], 0.5)
tap(*BTNS['8'], 0.5)
tap(*BTNS['='], 1.0)

print("\n--- Test 2: C, 5 / 0 = (expect Error) ---")
tap(*BTNS['C'], 0.5)   # clear
tap(*BTNS['5'], 0.5)
tap(*BTNS['/'], 0.5)
tap(*BTNS['0'], 0.5)
tap(*BTNS['='], 1.0)

print("\n=== Check final heap ===")
s.write(b"info\r\n")
print(">>> info")
time.sleep(2.0)

print("\n=== Back to home ===")
tap(30, 30, 1.5)

stop_flag = True
time.sleep(0.2)
s.close()
print("=== Done ===")

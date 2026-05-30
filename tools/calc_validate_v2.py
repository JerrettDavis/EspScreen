"""
Calculator + provisioning validation.
Mirror endpoint: GET http://10.0.0.88/api/screen (port 80)
Touch via HTTP API: POST http://10.0.0.88:8080/api/touch  (or serial)
"""
import serial, time, sys, threading, urllib.request, struct, os, json

port = "COM20"
baud = 115200
DEVICE_IP = "10.0.0.88"
SCREEN_URL = f"http://{DEVICE_IP}/api/screen"
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

def tap(x, y, delay=0.7):
    s.write((f"touch {x} {y}\r\n").encode())
    print(f">>> touch {x} {y}", flush=True)
    time.sleep(delay)

def send(cmd, delay=0.8):
    s.write((cmd + "\r\n").encode())
    print(f">>> {cmd}", flush=True)
    time.sleep(delay)

def fetch_screen(filename):
    try:
        req = urllib.request.urlopen(SCREEN_URL, timeout=5)
        data = req.read()
        path = os.path.join(TOOLS_DIR, filename)
        with open(path, 'wb') as f:
            f.write(data)
        print(f"[screen] Saved {len(data)} bytes -> {filename}")
        if data[:2] == b'BM':
            width = struct.unpack_from('<I', data, 18)[0]
            height = struct.unpack_from('<i', data, 22)[0]
            bpp = struct.unpack_from('<H', data, 28)[0]
            print(f"[screen] BMP {width}x{abs(height)} {bpp}bpp")
            return data
        else:
            print(f"[screen] Response (first 200 bytes): {data[:200]}")
            return data
    except Exception as e:
        print(f"[screen] Fetch failed: {e}")
        return None

def bmp_to_text(data):
    """Convert BMP to readable ASCII text by scanning for bright regions."""
    if not data or data[:2] != b'BM':
        return
    pixel_offset = struct.unpack_from('<I', data, 10)[0]
    width = struct.unpack_from('<I', data, 18)[0]
    height_raw = struct.unpack_from('<i', data, 22)[0]
    bpp = struct.unpack_from('<H', data, 28)[0]
    top_down = height_raw < 0
    height = abs(height_raw)
    if bpp != 16:
        print(f"  (not 16bpp: {bpp}bpp, skipping decode)")
        return

    row_size = ((width * 2 + 3) // 4) * 4
    chars = ' .:-=+*#%@'
    print(f"  === Screen dump ({width}x{height}) ===")
    # Print every 3rd row for readability
    for row in range(0, height, 3):
        src_row = row if top_down else (height - 1 - row)
        offset = pixel_offset + src_row * row_size
        line = ''
        for col in range(width):
            word = struct.unpack_from('<H', data, offset + col * 2)[0]
            r = ((word >> 11) & 0x1F) << 3
            g = ((word >> 5) & 0x3F) << 2
            b = (word & 0x1F) << 3
            lum = int(0.299*r + 0.587*g + 0.114*b)
            idx = min(int(lum/256*len(chars)), len(chars)-1)
            line += chars[idx]
        print(f"  {line}")

C0, C1, C2, C3 = 39, 119, 199, 279
R0, R1, R2, R3, R4 = 162, 231, 300, 369, 438

# Enable mirror
send("mirror on", 1.5)

# Navigate to calculator
print("\n=== Opening Calculator ===")
tap(30, 30, 1.5)   # ensure home
tap(83, 330, 2.5)  # calculator tile

print("\n=== Initial screen (expect display='0') ===")
data = fetch_screen("calc_0.bmp")
if data: bmp_to_text(data)

# Test 1: 7 + 8 =
print("\n--- 7 + 8 = ---")
tap(C0, R1, 0.5)   # 7
tap(C3, R3, 0.5)   # +
tap(C1, R1, 0.5)   # 8
tap(280, R4, 1.5)  # =
time.sleep(0.5)

print("\n=== Screen after 7+8= (expect '15') ===")
data = fetch_screen("calc_result15.bmp")
if data: bmp_to_text(data)

# Test 2: C 5 / 0 =
print("\n--- C 5 / 0 = ---")
tap(C0, R0, 0.5)   # C
tap(C1, R2, 0.5)   # 5
tap(C3, R0, 0.5)   # / (÷)
tap(80, R4, 0.5)   # 0
tap(280, R4, 1.5)  # =
time.sleep(0.5)

print("\n=== Screen after 5/0= (expect 'Error') ===")
data = fetch_screen("calc_error.bmp")
if data: bmp_to_text(data)

send("info", 1.5)
tap(30, 30, 1.5)   # back to home

stop_flag = True
time.sleep(0.2)
s.close()
print("\n=== Done ===")

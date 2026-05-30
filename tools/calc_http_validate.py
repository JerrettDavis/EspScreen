"""
Calculator validation via serial taps + HTTP BMP screenshot from mirror.
Device IP: 10.0.0.88 (HTTP API on :8080, web portal on :80)
Mirror endpoint: GET http://10.0.0.88/mirror (returns BMP)
"""
import serial, time, sys, threading, urllib.request, struct, os

port = "COM20"
baud = 115200
DEVICE_IP = "10.0.0.88"
MIRROR_URL = f"http://{DEVICE_IP}/mirror"
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))

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

def fetch_mirror(filename):
    """Fetch the BMP mirror image and save it."""
    try:
        req = urllib.request.urlopen(MIRROR_URL, timeout=5)
        data = req.read()
        path = os.path.join(TOOLS_DIR, filename)
        with open(path, 'wb') as f:
            f.write(data)
        print(f"[mirror] Saved {len(data)} bytes to {path}")
        # Parse BMP header to confirm it's valid
        if data[:2] == b'BM':
            width = struct.unpack_from('<I', data, 18)[0]
            height = struct.unpack_from('<I', data, 22)[0]
            print(f"[mirror] BMP: {width}x{height}")
        return path, data
    except Exception as e:
        print(f"[mirror] Fetch failed: {e}")
        return None, None

def bmp_to_ascii(data, scale=2):
    """Minimal BMP to ASCII art for 80x120 RGB565 BMP."""
    if not data or data[:2] != b'BM':
        return "(invalid BMP)"

    pixel_offset = struct.unpack_from('<I', data, 10)[0]
    width = struct.unpack_from('<I', data, 18)[0]
    height = struct.unpack_from('<i', data, 22)[0]  # signed (may be negative = top-down)
    bits_per_pixel = struct.unpack_from('<H', data, 28)[0]

    top_down = height < 0
    height = abs(height)

    print(f"[bmp_parse] {width}x{height} {bits_per_pixel}bpp offset={pixel_offset} top_down={top_down}")

    # For 16bpp, each pixel is 2 bytes
    if bits_per_pixel != 16:
        return f"(unsupported {bits_per_pixel}bpp)"

    row_size = ((width * 2 + 3) // 4) * 4  # padded to 4-byte boundary
    pixels = []
    for row in range(height):
        src_row = row if top_down else (height - 1 - row)
        offset = pixel_offset + src_row * row_size
        row_pixels = []
        for col in range(width):
            word = struct.unpack_from('<H', data, offset + col * 2)[0]
            # RGB565: R=bits[15:11], G=bits[10:5], B=bits[4:0]
            r = (word >> 11) & 0x1F
            g = (word >> 5) & 0x3F
            b = word & 0x1F
            # Convert to grayscale luminance
            r8 = r << 3
            g8 = g << 2
            b8 = b << 3
            lum = int(0.299 * r8 + 0.587 * g8 + 0.114 * b8)
            row_pixels.append(lum)
        pixels.append(row_pixels)

    # ASCII art: sample every `scale` pixels
    chars = ' .:-=+*#%@'
    lines = []
    for row in range(0, height, scale):
        line = ''
        for col in range(0, width, scale):
            lum = pixels[row][col]
            idx = int(lum / 256 * len(chars))
            idx = min(idx, len(chars)-1)
            line += chars[idx]
        lines.append(line)
    return '\n'.join(lines)

C0, C1, C2, C3 = 39, 119, 199, 279
R0, R1, R2, R3, R4 = 162, 231, 300, 369, 438

# Make sure mirror is ON
send("mirror on", 1.0)

# Navigate to calculator
print("\n=== Opening Calculator ===")
tap(30, 30, 1.5)   # ensure home
tap(83, 330, 2.5)  # calculator tile

# Give it time to render
time.sleep(1.0)
print("\n=== Screenshot: calculator open (showing '0') ===")
path, data = fetch_mirror("calc_initial.bmp")
if data:
    print(bmp_to_ascii(data, scale=2))

# Test 1: 7 + 8 =
print("\n--- Tapping: 7 + 8 = ---")
tap(C0, R1, 0.5)   # 7
tap(C3, R3, 0.5)   # +
tap(C1, R1, 0.5)   # 8
tap(280, R4, 1.5)  # =

print("\n=== Screenshot: after 7+8= (expect 15) ===")
time.sleep(0.8)
path, data = fetch_mirror("calc_7plus8.bmp")
if data:
    print(bmp_to_ascii(data, scale=2))

# Test 2: C 5 / 0 =
print("\n--- Tapping: C 5 / 0 = ---")
tap(C0, R0, 0.5)   # C
tap(C1, R2, 0.5)   # 5
tap(C3, R0, 0.5)   # /  (÷)
tap(80, R4, 0.5)   # 0
tap(280, R4, 1.5)  # =

print("\n=== Screenshot: after 5/0= (expect Error) ===")
time.sleep(0.8)
path, data = fetch_mirror("calc_div0.bmp")
if data:
    print(bmp_to_ascii(data, scale=2))

# Back to home
tap(30, 30, 1.5)
send("mirror off", 0.5)
send("info", 1.5)

stop_flag = True
time.sleep(0.2)
s.close()
print("\n=== Done ===")

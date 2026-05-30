"""Hardware validation script for ESPScreen firmware."""
import serial
import time
import sys
import io

# Force UTF-8 output on Windows to handle firmware's unicode arrows etc.
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

PORT = "COM20"
BAUD = 115200

def send_cmd(ser, cmd, wait=2.0):
    """Send command and collect response."""
    ser.write((cmd + "\n").encode())
    time.sleep(wait)
    lines = []
    while ser.in_waiting:
        line = ser.readline().decode(errors='replace').rstrip()
        lines.append(line)
    return lines

def drain(ser, duration=3.0):
    """Collect all output for a period."""
    lines = []
    deadline = time.time() + duration
    while time.time() < deadline:
        if ser.in_waiting:
            line = ser.readline().decode(errors='replace').rstrip()
            lines.append(line)
        else:
            time.sleep(0.05)
    return lines

print(f"Opening {PORT} at {BAUD}...")
with serial.Serial(PORT, BAUD, timeout=1) as ser:
    time.sleep(0.5)
    ser.reset_input_buffer()

    # ── Boot capture ──────────────────────────────────────────────────────
    print("\n=== BOOT OUTPUT (5s) ===")
    boot_lines = drain(ser, 5.0)
    for l in boot_lines:
        print(l)

    # ── info (heap) ────────────────────────────────────────────────────────
    print("\n=== info ===")
    resp = send_cmd(ser, "info", 2.0)
    for l in resp: print(l)

    # ── wifi status ───────────────────────────────────────────────────────
    print("\n=== wifi status ===")
    resp = send_cmd(ser, "wifi status", 2.0)
    for l in resp: print(l)

    # ── net status ────────────────────────────────────────────────────────
    print("\n=== net status ===")
    resp = send_cmd(ser, "net status", 2.0)
    for l in resp: print(l)

    # ── Check if we need to add wifi ──────────────────────────────────────
    combined = " ".join(resp + send_cmd(ser, "wifi status", 1.0))
    needs_wifi = "JDH-WIFI-01" not in combined and ("AP" in combined or "portal" in combined.lower() or "IDLE" in combined or "disconnected" in combined.lower())

    if needs_wifi:
        print("\n=== Adding WiFi network ===")
        resp = send_cmd(ser, 'wifi add "JDH-WIFI-01" "Slingo65"', 3.0)
        for l in resp: print(l)
        time.sleep(5)
        print("\n=== wifi status (after add) ===")
        resp = send_cmd(ser, "wifi status", 2.0)
        for l in resp: print(l)
        print("\n=== net status (after add) ===")
        resp = send_cmd(ser, "net status", 2.0)
        for l in resp: print(l)

    # ── Navigation sanity: touch a tile, go back ─────────────────────────
    print("\n=== NAVIGATION SANITY ===")
    # Touch tile 1 (approx first tile position ~85, 110)
    resp = send_cmd(ser, "touch 85 110", 2.0)
    for l in resp: print(l)

    print("\n=== info after push ===")
    resp = send_cmd(ser, "info", 2.0)
    for l in resp: print(l)

    # Go back
    resp = send_cmd(ser, "touch 30 30", 2.0)
    for l in resp: print(l)

    print("\n=== info after pop ===")
    resp = send_cmd(ser, "info", 2.0)
    for l in resp: print(l)

    # Touch tile 2 (~215, 110) then back
    resp = send_cmd(ser, "touch 215 110", 2.0)
    for l in resp: print(l)
    resp = send_cmd(ser, "touch 30 30", 2.0)
    for l in resp: print(l)

    print("\n=== info after second nav ===")
    resp = send_cmd(ser, "info", 2.0)
    for l in resp: print(l)

    # ── Claude profile ────────────────────────────────────────────────────
    print("\n=== claude profile list ===")
    resp = send_cmd(ser, "claude profile list", 3.0)
    for l in resp: print(l)

    # ── Claude poll ───────────────────────────────────────────────────────
    print("\n=== claude poll ===")
    resp = send_cmd(ser, "claude poll", 8.0)
    for l in resp: print(l)

    # ── Claude get ────────────────────────────────────────────────────────
    print("\n=== claude get ===")
    resp = send_cmd(ser, "claude get", 4.0)
    for l in resp: print(l)

print("\nDone.")

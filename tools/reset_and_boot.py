"""
Use esptool to trigger a hard reset via stub, then capture boot output.
esptool with --after hard_reset will reset the board normally.
"""
import subprocess, serial, time, sys

port = "COM20"
baud = 115200
esptool = r"C:\Users\jd\.platformio\packages\tool-esptoolpy\esptool.py"

print("[reset] Triggering hard reset via esptool...")
result = subprocess.run(
    [sys.executable, esptool, "--port", port, "--baud", "115200", "--after", "hard_reset", "read_mac"],
    capture_output=True, text=True, timeout=15
)
print(result.stdout[-300:] if result.stdout else "(no stdout)")
print(result.stderr[-300:] if result.stderr else "(no stderr)")
print(f"[esptool] exit code: {result.returncode}")

time.sleep(1.0)

# Now capture boot
try:
    s = serial.Serial(port, baud, timeout=0.5)
except Exception as e:
    print(f"[FAIL] Cannot open port: {e}")
    sys.exit(1)

print("[boot] Capturing boot log...")
start = time.time()
while time.time() - start < 12:
    line = s.readline()
    if line:
        d = line.decode('utf-8', errors='replace').rstrip().encode('ascii', errors='replace').decode('ascii')
        if d:
            print(d, flush=True)

s.close()
print("--- done ---")

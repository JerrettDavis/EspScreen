"""
serial_interact.py - Send commands to ESP32 and read responses
Usage: python serial_interact.py <duration_s> <cmd1> [cmd2] [cmd3] ...
Commands are sent with 1.5s gap between each, then output collected for duration.
"""
import serial, time, sys, threading

port = "COM20"
baud = 115200
duration = float(sys.argv[1]) if len(sys.argv) > 1 else 5.0
commands = sys.argv[2:] if len(sys.argv) > 2 else []

try:
    s = serial.Serial(port, baud, timeout=0.1)
except Exception as e:
    print(f"[FAIL] Cannot open {port}: {e}")
    sys.exit(1)

output_lines = []

def reader():
    while not stop_flag:
        line = s.readline()
        if line:
            decoded = line.decode('utf-8', errors='replace').rstrip()
            if decoded:
                output_lines.append(decoded)
                print(decoded.encode('ascii', errors='replace').decode('ascii'), flush=True)

stop_flag = False
t = threading.Thread(target=reader, daemon=True)
t.start()

time.sleep(0.3)
for cmd in commands:
    s.write((cmd + "\r\n").encode())
    print(f">>> {cmd}", flush=True)
    time.sleep(1.5)

# Collect remaining output
time.sleep(duration)
stop_flag = True
time.sleep(0.2)
s.close()

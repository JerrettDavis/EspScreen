import serial, time, sys

# Force UTF-8 output
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def send_cmd(ser, cmd, wait=3):
    ser.write((cmd + '\r\n').encode())
    time.sleep(wait)
    out = ser.read_all().decode(errors='replace')
    return out

with serial.Serial('COM20', 115200, timeout=2) as ser:
    # Wait for device to fully boot after flash
    print('Waiting for boot...', flush=True)
    time.sleep(6)
    ser.read_all()  # flush boot noise

    # Step 1: Add WiFi creds
    print('--- wifi add ---', flush=True)
    r = send_cmd(ser, 'wifi add "JDH-WIFI-01" "Slingo65"', 6)
    print(r, flush=True)

    # Step 2: Check wifi status
    print('--- wifi status ---', flush=True)
    r = send_cmd(ser, 'wifi status', 5)
    print(r, flush=True)

    # Step 3: Net status
    print('--- net status ---', flush=True)
    r = send_cmd(ser, 'net status', 4)
    print(r, flush=True)

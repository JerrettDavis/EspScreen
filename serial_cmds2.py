import serial, time, sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def send_cmd(ser, cmd, wait=3):
    ser.write((cmd + '\r\n').encode())
    time.sleep(wait)
    out = ser.read_all().decode(errors='replace')
    return out

with serial.Serial('COM20', 115200, timeout=2) as ser:
    time.sleep(1)
    ser.read_all()

    # Try help to see available commands
    print('--- help ---', flush=True)
    r = send_cmd(ser, 'help', 2)
    print(r, flush=True)

    # Try wifi connect
    print('--- wifi connect ---', flush=True)
    r = send_cmd(ser, 'wifi connect', 8)
    print(r, flush=True)

    # Check status
    print('--- wifi status ---', flush=True)
    r = send_cmd(ser, 'wifi status', 4)
    print(r, flush=True)

    print('--- net status ---', flush=True)
    r = send_cmd(ser, 'net status', 4)
    print(r, flush=True)

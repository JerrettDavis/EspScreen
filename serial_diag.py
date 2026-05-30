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

    # Check net status (IP, mode)
    print('--- net status ---', flush=True)
    r = send_cmd(ser, 'net status', 2)
    print(r, flush=True)

    # Check wifi status
    print('--- wifi status ---', flush=True)
    r = send_cmd(ser, 'wifi status', 2)
    print(r, flush=True)

    # Claude profile with token details
    print('--- claude profile list ---', flush=True)
    r = send_cmd(ser, 'claude profile list', 2)
    print(r, flush=True)

    # Full heap info
    print('--- info ---', flush=True)
    r = send_cmd(ser, 'info', 2)
    print(r, flush=True)

    # Try refreshing the token on-device (uses the stored refresh token)
    print('--- claude refresh ---', flush=True)
    r = send_cmd(ser, 'claude refresh', 12)
    print(r, flush=True)

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

    # Check info for wall clock / heap
    print('--- info (heap + clock) ---', flush=True)
    r = send_cmd(ser, 'info', 3)
    print(r, flush=True)

    # Wait a moment more for SNTP
    time.sleep(3)
    ser.read_all()

    # Check Claude profile
    print('--- claude profile list ---', flush=True)
    r = send_cmd(ser, 'claude profile list', 2)
    print(r, flush=True)

    # Force a poll
    print('--- claude poll ---', flush=True)
    r = send_cmd(ser, 'claude poll', 8)
    print(r, flush=True)

    # Get result
    print('--- claude get ---', flush=True)
    r = send_cmd(ser, 'claude get', 3)
    print(r, flush=True)

import serial, time, sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

def send_cmd(ser, cmd, wait=3):
    ser.write((cmd + '\r\n').encode())
    time.sleep(wait)
    out = ser.read_all().decode(errors='replace')
    return out

def drain(ser, wait=2):
    time.sleep(wait)
    return ser.read_all().decode(errors='replace')

with serial.Serial('COM20', 115200, timeout=2) as ser:
    time.sleep(1)
    ser.read_all()

    # Show stored networks before reset
    print('--- wifi list ---', flush=True)
    r = send_cmd(ser, 'wifi list', 2)
    print(r, flush=True)

    # Reboot so the device tries to STA-connect
    print('--- reset (rebooting...) ---', flush=True)
    ser.write(b'reset\r\n')
    # Capture boot log for ~15 seconds
    time.sleep(15)
    out = ser.read_all().decode(errors='replace')
    print(out, flush=True)

    # Now check status
    print('--- wifi status post-boot ---', flush=True)
    r = send_cmd(ser, 'wifi status', 4)
    print(r, flush=True)

    print('--- net status post-boot ---', flush=True)
    r = send_cmd(ser, 'net status', 4)
    print(r, flush=True)

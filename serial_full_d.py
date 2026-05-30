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

    # Check info (heap + uptime = sanity, wall clock will show after SNTP)
    print('--- info (pre-nav) ---', flush=True)
    r = send_cmd(ser, 'info', 2)
    print(r, flush=True)

    # The launcher grid is 300px wide, centred in 320px screen → left=10px
    # Tiles: 130x100 each, gap=SP_M=12px
    # Row 1: tile[0] Settings  x=10..140  tile[1] Touch Test x=152..282
    # Row 2: tile[2] About     x=10..140  tile[3] Claude Usage x=152..282
    # Row 3: tile[4] Calculator x=10..140
    # Grid starts at y=48 (aligned TOP_MID, offset 48), pad_all=8
    # Tile[3] center: x = 10 + 12 + 130 + 12 + 65 = 229?  y = 48 + 8 + 100 + 12 + 50 = 218?
    # More carefully:
    # grid x-start = (320-300)/2 = 10, pad=8 inside grid
    # tile0: x_center = 10 + 8 + 65 = 83,  y_center = 48 + 8 + 50 = 106
    # tile1: x_center = 10 + 8 + 130 + 12 + 65 = 225,  y_center = 106
    # tile2: x_center = 83,  y_center = 48 + 8 + 100 + 12 + 50 = 218
    # tile3: x_center = 225, y_center = 218
    # tile4: x_center = 83,  y_center = 330

    print('--- tapping Claude Usage tile (synthetic touch x=225 y=218) ---', flush=True)
    r = send_cmd(ser, 'touch 225 218', 4)
    print(r, flush=True)

    # Wait for Claude screen to load, then check SNTP
    time.sleep(2)
    ser.read_all()

    print('--- info after nav ---', flush=True)
    r = send_cmd(ser, 'info', 2)
    print(r, flush=True)

    # Now force poll
    print('--- claude poll ---', flush=True)
    r = send_cmd(ser, 'claude poll', 10)
    print(r, flush=True)

    # Get status
    print('--- claude get ---', flush=True)
    r = send_cmd(ser, 'claude get', 3)
    print(r, flush=True)

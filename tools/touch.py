#!/usr/bin/python3 -I
"""Root, on the phone: a virtual touchscreen for testing touch in LibreOffice (native/noparent.c).

    touch.py tap X Y
    touch.py drag X1 Y1 X2 Y2 MS [HOLD]  one finger: rest HOLD ms (default 0), then move over MS ms
    touch.py pinch X Y D1 D2 MS          two fingers centred on X,Y, from D1 to D2 px apart (vertical)

Coordinates are screen pixels (1080x2160, or W and H in the environment). The device is removed when
the script ends. Based on goals/gesture-pack/test/touch.py, with a hold before moving and two fingers.
"""
import fcntl, os, struct, sys, time

UI_SET_EVBIT, UI_SET_KEYBIT, UI_SET_ABSBIT, UI_SET_PROPBIT = 0x40045564, 0x40045565, 0x40045567, 0x4004556E
UI_DEV_SETUP, UI_ABS_SETUP, UI_DEV_CREATE, UI_DEV_DESTROY = 0x405C5503, 0x401C5504, 0x5501, 0x5502
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
BTN_TOUCH, INPUT_PROP_DIRECT = 0x14A, 1
ABS_X, ABS_Y, ABS_MT_SLOT, ABS_MT_X, ABS_MT_Y, ABS_MT_ID = 0x00, 0x01, 0x2F, 0x35, 0x36, 0x39
W = int(os.environ.get("W", 1080))
H = int(os.environ.get("H", 2160))


def ev(fd, t, c, v):
    os.write(fd, struct.pack("llHHi", 0, 0, t, c, v))


def abs_setup(fd, code, maximum):
    fcntl.ioctl(fd, UI_SET_ABSBIT, code)
    fcntl.ioctl(fd, UI_ABS_SETUP, struct.pack("HH6i", code, 0, 0, 0, maximum, 0, 0, 0))


def device():
    fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
    fcntl.ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH)
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_ABS)
    fcntl.ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT)
    for code, top in ((ABS_X, W - 1), (ABS_Y, H - 1), (ABS_MT_SLOT, 9), (ABS_MT_X, W - 1),
                      (ABS_MT_Y, H - 1), (ABS_MT_ID, 65535)):
        abs_setup(fd, code, top)
    fcntl.ioctl(fd, UI_DEV_SETUP, struct.pack("HHHH80sI", 0x06, 1, 3, 1, b"libreoffice-test-touch", 0))
    fcntl.ioctl(fd, UI_DEV_CREATE)
    time.sleep(1.5)  # let the compositor pick the device up
    return fd


def frame(fd, fingers, first=False):
    """fingers: list of (slot, x, y); first=True puts them down."""
    for slot, x, y in fingers:
        ev(fd, EV_ABS, ABS_MT_SLOT, slot)
        if first:
            ev(fd, EV_ABS, ABS_MT_ID, slot + 1)
        ev(fd, EV_ABS, ABS_MT_X, x); ev(fd, EV_ABS, ABS_MT_Y, y)
    if first:
        ev(fd, EV_KEY, BTN_TOUCH, 1)
    ev(fd, EV_ABS, ABS_X, fingers[0][1]); ev(fd, EV_ABS, ABS_Y, fingers[0][2])
    ev(fd, EV_SYN, 0, 0)


def lift(fd, slots):
    for slot in slots:
        ev(fd, EV_ABS, ABS_MT_SLOT, slot); ev(fd, EV_ABS, ABS_MT_ID, -1)
    ev(fd, EV_KEY, BTN_TOUCH, 0); ev(fd, EV_SYN, 0, 0)


def move(fd, start, end, ms):
    """start, end: lists of (slot, x, y) with the same slots; a frame every 16 ms."""
    t0 = time.time()
    while True:
        time.sleep(0.016)
        f = min(1.0, (time.time() - t0) / max(ms / 1000, 0.001))
        frame(fd, [(s, round(x + (x2 - x) * f), round(y + (y2 - y) * f))
                   for (s, x, y), (_, x2, y2) in zip(start, end)])
        if f >= 1.0:
            return


def main():
    cmd, a = sys.argv[1], [int(v) for v in sys.argv[2:]]
    fd = device()
    try:
        if cmd == "tap":
            frame(fd, [(0, a[0], a[1])], first=True); time.sleep(0.06); lift(fd, [0])
        elif cmd == "drag":
            x1, y1, x2, y2, ms = a[:5]
            hold = a[5] if len(a) > 5 else 0
            start = [(0, x1, y1)]
            frame(fd, start, first=True)
            move(fd, start, start, hold)  # a resting finger still reports now and then
            move(fd, start, [(0, x2, y2)], ms)
            lift(fd, [0])
        elif cmd == "pinch":
            x, y, d1, d2, ms = a
            start = [(0, x, y - d1 // 2), (1, x, y + d1 // 2)]
            frame(fd, start, first=True)
            move(fd, start, [(0, x, y - d2 // 2), (1, x, y + d2 // 2)], ms)
            lift(fd, [0, 1])
        else:
            sys.exit(__doc__)
        time.sleep(0.5)
    finally:
        fcntl.ioctl(fd, UI_DEV_DESTROY)
        os.close(fd)


if __name__ == "__main__":
    main()

#!/usr/bin/python3 -I
"""Root, on the phone: a virtual keyboard for tests (clipboard in native/noparent.c).

    keys.py ctrl+v " ok" ctrl+a ctrl+c

Each argument is either a chord like ctrl+v or shift+a, or text in lower-case letters, digits and
spaces, typed one key at a time. The keys go to the focused window. The device is removed when the
script ends, like tools/touch.py.
"""
import fcntl, os, struct, sys, time

UI_SET_EVBIT, UI_SET_KEYBIT = 0x40045564, 0x40045565
UI_DEV_SETUP, UI_DEV_CREATE, UI_DEV_DESTROY = 0x405C5503, 0x5501, 0x5502
EV_SYN, EV_KEY = 0, 1
MODS = {"ctrl": 29, "shift": 42, "alt": 56}
KEYS = {c: k for k, c in zip(
    (30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44),
    "abcdefghijklmnopqrstuvwxyz")}
KEYS.update({str(d): 2 + (d - 1) % 10 for d in range(1, 11)} | {"0": 11, " ": 57, "enter": 28, "esc": 1})


def ev(fd, t, c, v):
    os.write(fd, struct.pack("llHHi", 0, 0, t, c, v))


def press(fd, codes):
    for c in codes:
        ev(fd, EV_KEY, c, 1); ev(fd, EV_SYN, 0, 0)
    for c in reversed(codes):
        ev(fd, EV_KEY, c, 0); ev(fd, EV_SYN, 0, 0)
    time.sleep(0.08)


def main(args):
    fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
    for code in set(KEYS.values()) | set(MODS.values()):
        fcntl.ioctl(fd, UI_SET_KEYBIT, code)
    fcntl.ioctl(fd, UI_DEV_SETUP, struct.pack("HHHH80sI", 0x06, 1, 4, 1, b"libreoffice-test-keys", 0))
    fcntl.ioctl(fd, UI_DEV_CREATE)
    time.sleep(1.5)  # let the compositor pick the device up
    try:
        for arg in args:
            parts = arg.split("+")
            if len(parts) > 1 and all(p in MODS for p in parts[:-1]) and parts[-1] in KEYS:
                press(fd, [MODS[p] for p in parts[:-1]] + [KEYS[parts[-1]]])
            elif arg in KEYS:
                press(fd, [KEYS[arg]])
            else:
                for ch in arg:
                    press(fd, [KEYS[ch]])
            time.sleep(0.4)
    finally:
        time.sleep(0.3)
        fcntl.ioctl(fd, UI_DEV_DESTROY)
        os.close(fd)


if __name__ == "__main__":
    main(sys.argv[1:])

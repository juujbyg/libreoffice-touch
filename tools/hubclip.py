#!/usr/bin/python3 -I
"""As phablet, on the phone: read or set Content Hub's clipboard, the one Qt apps use.

    hubclip.py put TEXT    what a Qt app does on copy (CreatePaste, as text/plain)
    hubclip.py get         prints the latest text (GetLatestPasteData)

Run unconfined: Content Hub only lets the focused app paste, but lets unconfined processes through.
"""
import re, struct, subprocess, sys

DEST = ["--session", "--dest", "com.lomiri.content.dbus.Service", "--object-path", "/",
        "--method"]
IFACE = "com.lomiri.content.dbus.Service."


def put(text):
    fmt, data = b"text/plain", text.encode()
    head = struct.pack("5i", 1, 20, len(fmt), 20 + len(fmt), len(data))
    blob = head + fmt + data
    ay = "[" + ", ".join("byte 0x%02x" % b for b in blob) + "]"
    subprocess.run(["gdbus", "call", *DEST, IFACE + "CreatePaste", "", "", ay, "['text/plain']"], check=True)


def get():
    out = subprocess.run(["gdbus", "call", *DEST, IFACE + "GetLatestPasteData", ""],
                         check=True, capture_output=True, text=True).stdout
    if "b'" in out:  # a nul-terminated array is printed as a bytestring
        raise SystemExit("unexpected format: " + out[:200])
    blob = bytes(int(h, 16) for h in re.findall(r"0x([0-9a-f]{2})", out))
    if len(blob) < 4:
        print("")
        return
    count = struct.unpack_from("i", blob)[0]
    for i in range(min(count, 16)):
        fo, fs, do, ds = struct.unpack_from("4i", blob, 4 + 16 * i)
        if blob[fo:fo + fs].decode("latin-1").startswith("text/plain"):
            print(blob[do:do + ds].decode("utf-8", "replace"))
            return
    print("")


if __name__ == "__main__":
    put(sys.argv[2]) if sys.argv[1] == "put" else get()

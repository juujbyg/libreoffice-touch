#!/usr/bin/env python3
"""Root: open files through the LibreOffice desktop entries (as tapping a file does), screenshot each.
Usage: openfiles.py app=/path [app=/path ...]"""
import subprocess, sys, time, dbus, urllib.parse
b = dbus.SystemBus()
scr = dbus.Interface(b.get_object('com.canonical.Unity.Screen', '/com/canonical/Unity/Screen'), 'com.canonical.Unity.Screen')
cookie = scr.keepDisplayOn()
ENV = "export XDG_RUNTIME_DIR=/run/user/32011 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus"
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
try:
    for arg in sys.argv[1:]:
        app, path = arg.split("=", 1)
        uri = "file://" + urllib.parse.quote(path)
        sh("pkill -x soffice.bin; sleep 4")
        r = sh(f"su phablet -c '{ENV}; timeout 10 lomiri-app-launch libreoffice.juujbyg_{app}_0.2.3 \"{uri}\"'")
        time.sleep(30)
        for _ in range(2):
            sh(f"timeout 20 mirscreencast -m /run/user/32011/mir_socket_trusted -n 1 -f /tmp/of-{app}.raw"); time.sleep(1)
        ps = sh("pgrep -a soffice.bin").stdout.strip()
        print(app, uri, "->", "running: " + ps[ps.find('--'):][:120] if ps else "NOT RUNNING", r.stderr.strip()[:200])
    sh("pkill -x soffice.bin")
finally:
    scr.removeDisplayOnRequest(cookie)

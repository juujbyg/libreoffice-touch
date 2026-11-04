#!/usr/bin/env python3
"""Root: launch each LibreOffice app through Lomiri like the app drawer does, screenshot it, close it.
Keeps the display on meanwhile. Usage: suite.py app [app...]"""
import subprocess, sys, time, dbus
b = dbus.SystemBus()
scr = dbus.Interface(b.get_object('com.canonical.Unity.Screen', '/com/canonical/Unity/Screen'), 'com.canonical.Unity.Screen')
cookie = scr.keepDisplayOn()
ENV = "export XDG_RUNTIME_DIR=/run/user/32011 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus"
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True).stdout
try:
    for app in sys.argv[1:]:
        sh("pkill -x soffice.bin; sleep 4")
        sh(f'su phablet -c "{ENV}; timeout 10 lomiri-app-launch libreoffice.juujbyg_{app}_0.1.0"')
        time.sleep(int(__import__("os").environ.get("WAIT", "30")))
        for _ in range(2):
            sh(f"timeout 20 mirscreencast -m /run/user/32011/mir_socket_trusted -n 1 -f /tmp/su-{app}.raw"); time.sleep(1)
        alive = sh("pgrep -x soffice.bin").strip()
        print(app, "running" if alive else "NOT RUNNING", sh("grep -h lo-shim /home/phablet/.cache/libreoffice.juujbyg/last.log | grep -v hints").strip())
    sh("pkill -x soffice.bin")
finally:
    scr.removeDisplayOnRequest(cookie)

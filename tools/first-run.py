#!/usr/bin/env python3
"""Root, on the phone: test the click's first launch without tapping.

Launches the Documents app through Lomiri with LibreOffice not yet installed, screenshots the
first-use screen, then runs the same installer from the shell (as the user tapping Install
would), and checks that LibreOffice opens while the screen is still up and survives it closing.
Screens go to /tmp/fr-<n>.raw. Log on stdout. Keeps the display on meanwhile."""
import os, subprocess, time, dbus

APP = "libreoffice.juujbyg_writer_0.4.1"
DATA = "/home/phablet/.local/share/libreoffice.juujbyg"
CLICK = "/opt/click.ubuntu.com/libreoffice.juujbyg/current"
ENV = "export XDG_RUNTIME_DIR=/run/user/32011 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus"
scr = dbus.Interface(dbus.SystemBus().get_object('com.canonical.Unity.Screen', '/com/canonical/Unity/Screen'),
                     'com.canonical.Unity.Screen')


def sh(c):
    return subprocess.run(c, shell=True, capture_output=True, text=True).stdout.strip()


def shot(n):
    for _ in range(2):
        sh(f"timeout 20 mirscreencast -m /run/user/32011/mir_socket_trusted -n 1 -f /tmp/fr-{n}.raw")
        time.sleep(1)


def state(label):
    print(time.strftime("%T"), label, "| soffice:", sh("pgrep -x soffice.bin") or "-",
          "| screen:", sh("pgrep -f '[q]mlscene.*installer'") or "-",
          "| units:", sh(f"su phablet -c '{ENV}; systemctl --user list-units --no-legend \"lomiri-app-launch--application-click--office*\"' | awk '{{print $1, $4}}'"),
          flush=True)


cookie = scr.keepDisplayOn()
try:
    sh(f'su phablet -c "{ENV}; timeout 10 lomiri-app-launch {APP}"')
    time.sleep(10)
    state("launched")
    shot(1)
    t = time.time()
    out = subprocess.run(["su", "phablet", "-c", f"OFFICE_DATA={DATA} python3 -I {CLICK}/installer/installer.py 2>&1 | grep -v progress"],
                         capture_output=True, text=True)
    print("install took %ds:" % (time.time() - t), out.stdout.strip()[-500:], flush=True)
    for i in range(6):
        time.sleep(10)
        state("after install +%ds" % (10 * (i + 1)))
    shot(2)
    sh("pkill -f '[q]mlscene.*installer'")
    time.sleep(15)
    state("screen closed")
    shot(3)
finally:
    scr.removeDisplayOnRequest(cookie)
print("END", flush=True)

#!/usr/bin/env python3
"""Root, on the phone: open a test document in each app of the installed click and screenshot it
for the store (/tmp/store-<app>.raw). Closes only the click's own LibreOffice afterwards."""
import glob, os, subprocess, sys, time, dbus
DOCS = "/home/phablet/Documents/LibreOffice test"
SHOTS = {"writer": "Report.docx", "calc": "Budget.xlsx", "impress": "Slides.pptx"}
ENV = "export XDG_RUNTIME_DIR=/run/user/32011 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus"
scr = dbus.Interface(dbus.SystemBus().get_object('com.canonical.Unity.Screen', '/com/canonical/Unity/Screen'),
                     'com.canonical.Unity.Screen')

def ours():
    pids = []
    for exe in glob.glob("/proc/[0-9]*/exe"):
        try:
            if "/libreoffice.juujbyg/" in os.readlink(exe):
                pids.append(exe.split("/")[2])
        except OSError:
            pass
    return pids

def stop():
    for p in ours():
        subprocess.run(["kill", "-9", p])
    time.sleep(3)

cookie = scr.keepDisplayOn()
try:
    for app in sys.argv[1:] or SHOTS:
        stop()
        uri = "file://" + os.path.join(DOCS, SHOTS[app]).replace(" ", "%20")
        subprocess.run(["su", "phablet", "-c", f'{ENV}; timeout 10 lomiri-app-launch libreoffice.juujbyg_{app}_0.2.0 "{uri}"'])
        time.sleep(int(os.environ.get("WAIT", "25")))
        for _ in range(2):
            subprocess.run(["timeout", "20", "mirscreencast", "-m", "/run/user/32011/mir_socket_trusted", "-n", "1", "-f", f"/tmp/store-{app}.raw"],
                           capture_output=True)
            time.sleep(1)
        print(app, "running" if ours() else "NOT RUNNING", flush=True)
    stop()
finally:
    scr.removeDisplayOnRequest(cookie)
print("END", flush=True)

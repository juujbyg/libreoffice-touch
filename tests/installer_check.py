"""PC check of installer/installer.py with a fake LibreOffice archive (no network):
a normal install, two installs at once, and a download with the wrong hash."""
import hashlib, io, os, sys, tarfile, tempfile, threading
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "installer"))
import installer

def deb(files):
    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode="w:xz") as t:
        for name, body in files.items():
            info = tarfile.TarInfo(name); info.size = len(body); info.mode = 0o755
            t.addfile(info, io.BytesIO(body))
    out = b"!<arch>\n"
    for name, body in (("debian-binary", b"2.0\n"), ("data.tar.xz", data.getvalue())):
        out += ("%-16s%-12s%-6s%-6s%-8s%-10d`\n" % (name + "/", 0, 0, 0, "100644", len(body))).encode()
        out += body + (b"\n" if len(body) % 2 else b"")
    return out

tmp = tempfile.mkdtemp()
archive = os.path.join(tmp, "lo.tar.gz")
with tarfile.open(archive, "w:gz") as t:
    for name, files in (("core.deb", {"./opt/libreoffice26.8/program/soffice.bin": b"#!/bin/sh\n", "./usr/share/x": b"x"}),
                        ("kde-integration.deb", {"./opt/libreoffice26.8/program/kde": b"k"})):
        body = deb(files); info = tarfile.TarInfo("LibreOffice/DEBS/" + name); info.size = len(body)
        t.addfile(info, io.BytesIO(body))
installer.URL = "file://" + archive
installer.SHA256 = hashlib.sha256(open(archive, "rb").read()).hexdigest()
installer.NEEDED_MB = 1
installer.platform.machine = lambda: "aarch64"
downloads = []
real = installer._download
installer._download = lambda target: (downloads.append(1), real(target))
installer._send = lambda *a: None

data = os.path.join(tmp, "one")
installer.install(data)
prog = os.path.join(data, "libreoffice/opt/libreoffice26.8/program")
assert os.path.exists(prog + "/soffice.bin") and not os.path.exists(prog + "/kde"), "install"
assert not os.path.exists(os.path.join(data, "libreoffice/usr")), "only opt/"
assert sorted(os.listdir(data)) == [".install.lock", "libreoffice"], os.listdir(data)
print("ok normal install")

downloads.clear(); data = os.path.join(tmp, "two"); errors = []
def run():
    try: installer.install(data)
    except Exception as e: errors.append(e)
threads = [threading.Thread(target=run) for _ in range(2)]
[t.start() for t in threads]; [t.join() for t in threads]
assert not errors and len(downloads) == 1 and installer.installed(data), (errors, downloads)
print("ok two at once: one download, both fine")

installer.SHA256 = "0" * 64; data = os.path.join(tmp, "bad")
try:
    installer.install(data); raise SystemExit("bad hash accepted")
except installer.InstallError as e:
    assert "SHA-256" in str(e)
assert os.listdir(data) == [".install.lock"], os.listdir(data)
print("ok wrong hash refused, nothing left")

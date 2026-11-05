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
    for name, files in (("core.deb", {"./opt/libreoffice26.8/program/soffice.bin": b"#!/bin/sh\n", "./usr/share/x": b"x",
                                      "./opt/libreoffice26.8/share/extensions/dict-xx/xx.dic": b"shipped"}),
                        ("kde-integration.deb", {"./opt/libreoffice26.8/program/kde": b"k"})):
        body = deb(files); info = tarfile.TarInfo("LibreOffice/DEBS/" + name); info.size = len(body)
        t.addfile(info, io.BytesIO(body))
installer.URL = "file://" + archive
installer.SHA256 = hashlib.sha256(open(archive, "rb").read()).hexdigest()
installer.NEEDED_MB = 1
installer.platform.machine = lambda: "aarch64"
downloads = []
real = installer._download
installer._download = lambda target, *rest: (downloads.append(1), real(target, *rest))
installer._send = lambda *a: None
real_phone_language = installer.phone_language
installer.phone_language = lambda environ=None: None   # no language pack in the first tests

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

# Language packs ----------------------------------------------------------------------------------
installer.SHA256 = hashlib.sha256(open(archive, "rb").read()).hexdigest()
pack = os.path.join(tmp, "pack-xx.tar.gz")
with tarfile.open(pack, "w:gz") as t:
    for name, files in (("libobasis-xx.deb", {"./opt/libreoffice26.8/program/resource/xx/LC_MESSAGES/sw.mo": b"mo",
                                              "./opt/libreoffice26.8/share/registry/res/registry_xx.xcd": b"xcd"}),
                        ("dict-xx.deb", {"./opt/libreoffice26.8/share/extensions/dict-xx/xx.dic": b"shipped",
                                         "./opt/libreoffice26.8/share/extensions/dict-xx/xx.aff": b"aff"})):
        body = deb(files); info = tarfile.TarInfo("Pack/DEBS/" + name); info.size = len(body)
        t.addfile(info, io.BytesIO(body))
entry = {"code": "xx", "name": "Xx", "english": "Xish", "size": os.path.getsize(pack), "dictionary": True,
         "sha256": hashlib.sha256(open(pack, "rb").read()).hexdigest()}
real_catalog = installer.catalog
installer.catalog = lambda: [entry, {"code": "pt-BR"}, {"code": "es"}]
installer.LANGUAGE_URL = "file://" + os.path.join(tmp, "pack-%s.tar.gz")

data = os.path.join(tmp, "lang")
try:
    installer.install_language("xx", data); raise SystemExit("pack installed without LibreOffice")
except installer.InstallError:
    pass
installer.install(data)
lo = os.path.join(data, "libreoffice/opt/libreoffice26.8")
installer.install_language("xx", data)
assert installer.installed_languages(data) == ["xx"]
assert open(lo + "/program/resource/xx/LC_MESSAGES/sw.mo").read() == "mo"
record = open(os.path.join(data, "libreoffice/languages/xx.files")).read().split()
assert "opt/libreoffice26.8/share/extensions/dict-xx/xx.dic" not in record and len(record) == 3, record
assert not [n for n in os.listdir(data) if n.startswith("install-")]
print("ok language pack added; files LibreOffice already had aren't claimed")

installer.remove_language("xx", data)
assert installer.installed_languages(data) == []
assert not os.path.exists(lo + "/program/resource/xx") and not os.path.exists(lo + "/share/extensions/dict-xx/xx.aff")
assert open(lo + "/share/extensions/dict-xx/xx.dic").read() == "shipped" and os.path.exists(lo + "/program/soffice.bin")
assert os.path.isdir(lo + "/program/resource") is False or os.listdir(lo + "/program/resource") == []
print("ok language pack removed; LibreOffice's own files stay")

entry["sha256"] = "0" * 64
try:
    installer.install_language("xx", data); raise SystemExit("bad pack hash accepted")
except installer.InstallError as e:
    assert "SHA-256" in str(e)
assert installer.installed_languages(data) == [] and not os.path.exists(lo + "/program/resource/xx")
assert not [n for n in os.listdir(data) if n.startswith("install-")]
print("ok language pack with a wrong hash refused, nothing added")
try:
    installer.install_language("../../x", data); raise SystemExit("unknown code accepted")
except installer.InstallError:
    pass

pl = real_phone_language
assert pl({"LANG": "es_ES.UTF-8"}) == "es" and pl({"LANGUAGE": "pt_BR:pt", "LANG": "en_US.UTF-8"}) == "pt-BR"
assert pl({"LANG": "en_US.UTF-8"}) is None and pl({"LANG": "C"}) is None and pl({}) is None
print("ok phone language")

entry["sha256"] = hashlib.sha256(open(pack, "rb").read()).hexdigest()
installer.phone_language = lambda environ=None: "xx"
data = os.path.join(tmp, "first")
installer.install(data)
assert installer.installed_languages(data) == ["xx"]
print("ok first install adds the phone's language")

installer.catalog = real_catalog
codes = {e["code"] for e in installer.catalog()}
assert len(codes) == 122 and "qtz" not in codes and {"es", "de", "fr", "pt-BR", "en-GB"} <= codes
assert all(len(e["sha256"]) == 64 and e["size"] > 0 and e["name"] for e in installer.catalog())
assert real_phone_language({"LANG": "de_DE.UTF-8"}) == "de" and real_phone_language({"LANG": "en_GB.UTF-8"}) == "en-GB"
print("ok real catalog")

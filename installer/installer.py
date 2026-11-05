"""Download the official LibreOffice for arm64 and unpack it into the app's data folder.

The download is The Document Foundation's own build, checked against a fixed hash, so what runs
is exactly what TDF published. Nothing is installed system-wide and no root is needed.
Runs inside the installer screen (pyotherside); progress goes to QML as events.
"""
import fcntl
import hashlib
import io
import json
import os
import platform
import shutil
import tarfile
import tempfile
import threading
import urllib.request

try:
    import pyotherside
except ImportError:  # run from the command line for testing
    pyotherside = None

VERSION = "26.8.2"
BUILD = VERSION + ".2"
# The archive keeps every release; the stable folder drops a release when the next one arrives.
URL = ("https://downloadarchive.documentfoundation.org/libreoffice/old/%s/deb/aarch64/"
       "LibreOffice_%s_Linux_aarch64_deb.tar.gz" % (BUILD, BUILD))
SHA256 = "54a780b666fcda7510c2eebe85265b71dd209db280fc16882fca5589015ca505"
DOWNLOAD_MB = 200
# Download, unpacked copy and some slack.
NEEDED_MB = 1200
CHUNK = 1 << 20
# Parts that need Java or KDE, or update LibreOffice behind the store's back.
SKIP = ("kde-integration", "onlineupdate", "report-builder", "nlpsolver")
USER_AGENT = "LibreOffice-UbuntuTouch-port/0.1 (+https://github.com/juujbyg/libreoffice-touch)"


class InstallError(Exception):
    pass


def _send(*args):
    if pyotherside:
        pyotherside.send(*args)
    else:
        print(*args)


def _data():
    return os.environ.get("OFFICE_DATA") or os.path.expanduser("~/.local/share/libreoffice.juujbyg")


def installed(data=None):
    opt = os.path.join(data or _data(), "libreoffice", "opt")
    if not os.path.isdir(opt):
        return False
    return any(os.path.exists(os.path.join(opt, n, "program", "soffice.bin")) for n in os.listdir(opt))


def _download(target, url=None, sha256=None, step="download", size=None):
    url, sha256 = url or URL, sha256 or SHA256
    sha = hashlib.sha256()
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        response = urllib.request.urlopen(request, timeout=30)
    except OSError as e:
        raise InstallError("Couldn't reach The Document Foundation's server. Check the connection. (%s)" % e)
    with response, open(target, "wb") as out:
        total = int(response.headers.get("Content-Length") or size or DOWNLOAD_MB << 20)
        done = 0
        while True:
            chunk = response.read(CHUNK)
            if not chunk:
                break
            out.write(chunk)
            sha.update(chunk)
            done += len(chunk)
            _send("progress", step, min(done / total, 1.0))
    if sha.hexdigest() != sha256:
        raise InstallError("The download failed its security check (wrong SHA-256), so it was thrown away.")


def _deb_members(stream):
    """Yield (name, bytes) for each member of an ar archive, which is what a .deb is."""
    if stream.read(8) != b"!<arch>\n":
        raise InstallError("The download is damaged.")
    while True:
        header = stream.read(60)
        if len(header) < 60:
            return
        name = header[:16].decode().strip().rstrip("/")
        size = int(header[48:58])
        yield name, stream.read(size)
        if size % 2:
            stream.read(1)


def _unpack_deb(stream, root):
    for name, data in _deb_members(stream):
        if name.startswith("data.tar"):
            with tarfile.open(fileobj=io.BytesIO(data)) as archive:
                # Menu entries and the like sit outside opt/ and aren't needed.
                members = (m for m in archive if m.name.startswith("./opt/"))
                archive.extractall(root, members=members, filter="data")


def install(data=None):
    """Download and unpack LibreOffice into <data>/libreoffice. A failed install leaves nothing."""
    data = data or _data()
    if platform.machine() != "aarch64":
        raise InstallError("This app needs an arm64 phone.")
    os.makedirs(data, exist_ok=True)
    # Two apps of the suite opened on first use would each start an install: the second one
    # waits here for the first, and then finds LibreOffice already there.
    with open(os.path.join(data, ".install.lock"), "w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            _send("progress", "wait", 0)
            fcntl.flock(lock, fcntl.LOCK_EX)
        if not installed(data):
            _install(data)
            # Menus in the phone's language from the start, when TDF has a pack for it. LibreOffice
            # itself works without it, so a failure here doesn't fail the install.
            code = phone_language()
            if code:
                try:
                    _install_language(data, code)
                except Exception as e:  # noqa: BLE001
                    _send("progress", "language-failed", 0)
                    print("language pack %s: %s" % (code, e))


def _install(data):
    # Leftovers of an attempt that was cancelled (the screen closed) or cut short.
    for name in os.listdir(data):
        if name.startswith("install-"):
            shutil.rmtree(os.path.join(data, name), ignore_errors=True)
    free_mb = shutil.disk_usage(data).free >> 20
    if free_mb < NEEDED_MB:
        raise InstallError("Not enough free space: about %d MB is needed and %d MB is free." % (NEEDED_MB, free_mb))
    work = tempfile.mkdtemp(prefix="install-", dir=data)
    try:
        download = os.path.join(work, "libreoffice.tar.gz")
        _download(download)
        unpacked = os.path.join(work, "libreoffice")
        with tarfile.open(download) as archive:
            debs = [m for m in archive.getmembers() if m.name.endswith(".deb") and not any(s in m.name for s in SKIP)]
            for index, member in enumerate(debs):
                _unpack_deb(archive.extractfile(member), unpacked)
                _send("progress", "unpack", (index + 1) / len(debs))
        if not installed(work):
            raise InstallError("The download is missing LibreOffice's program.")
        target = os.path.join(data, "libreoffice")
        shutil.rmtree(target, ignore_errors=True)
        os.rename(unpacked, target)
    finally:
        shutil.rmtree(work, ignore_errors=True)


# Language packs: menus, help texts and spelling dictionaries for other languages, published by TDF
# for the same build. languages.json (made by tools/langpacks.py) pins each pack's SHA-256, after
# checking TDF's GPG signature on it. A pack is unpacked into the LibreOffice folder, and the files
# it added are listed in libreoffice/languages/<code>.files so that removing it takes only those.
LANGUAGES_JSON = os.path.join(os.path.dirname(os.path.abspath(__file__)), "languages.json")
LANGUAGE_URL = URL.rsplit("/", 1)[0] + "/LibreOffice_%s_Linux_aarch64_deb_langpack_%%s.tar.gz" % BUILD


def catalog():
    """Every language pack TDF publishes for this build, by English name."""
    with open(LANGUAGES_JSON, encoding="utf-8") as f:
        return json.load(f)["languages"]


def _pack(code):
    for entry in catalog():
        if entry["code"] == code:
            return entry
    raise InstallError("There is no LibreOffice language pack for \"%s\"." % code)


def _records(data):
    return os.path.join(data, "libreoffice", "languages")


def installed_languages(data=None):
    folder = _records(data or _data())
    if not os.path.isdir(folder):
        return []
    return sorted(n[:-len(".files")] for n in os.listdir(folder) if n.endswith(".files"))


def phone_language(environ=None):
    """The language pack that matches the phone's language, or None (English is built in)."""
    environ = os.environ if environ is None else environ
    value = ""
    for name in ("LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"):
        value = (environ.get(name) or "").split(":")[0]
        if value:
            break
    value = value.split(".")[0].split("@")[0]
    if not value or value in ("C", "POSIX"):
        return None
    codes = {entry["code"] for entry in catalog()}
    parts = value.replace("_", "-").split("-")
    for candidate in ("-".join(parts[:2]), parts[0]):
        if candidate in codes:
            return candidate
    return None


def _install_language(data, code):
    entry = _pack(code)
    if code in installed_languages(data):
        return
    work = tempfile.mkdtemp(prefix="install-language-", dir=data)
    try:
        download = os.path.join(work, "pack.tar.gz")
        _download(download, LANGUAGE_URL % code, entry["sha256"], "language", entry["size"])
        unpacked = os.path.join(work, "pack")
        with tarfile.open(download) as archive:
            for member in archive.getmembers():
                if member.name.endswith(".deb"):
                    _unpack_deb(archive.extractfile(member), unpacked)
        # Move the pack's files into LibreOffice's folder. Files that are already there (such as a
        # dictionary that LibreOffice itself ships) belong to LibreOffice and stay out of the list.
        root = os.path.join(data, "libreoffice")
        added = []
        for folder, _dirs, files in os.walk(unpacked):
            for name in files:
                source = os.path.join(folder, name)
                relative = os.path.relpath(source, unpacked)
                target = os.path.join(root, relative)
                if os.path.lexists(target):
                    continue
                os.makedirs(os.path.dirname(target), exist_ok=True)
                os.rename(source, target)
                added.append(relative)
        os.makedirs(_records(data), exist_ok=True)
        record = os.path.join(_records(data), code + ".files")
        with open(record + ".new", "w", encoding="utf-8") as f:
            f.write("".join(path + "\n" for path in sorted(added)))
        os.rename(record + ".new", record)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def install_language(code, data=None):
    data = data or _data()
    _pack(code)
    if not installed(data):
        raise InstallError("Install LibreOffice first.")
    with open(os.path.join(data, ".install.lock"), "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if not installed(data):
            raise InstallError("Install LibreOffice first.")
        _install_language(data, code)


def remove_language(code, data=None):
    data = data or _data()
    record = os.path.join(_records(data), code + ".files")
    if code not in installed_languages(data):
        return
    root = os.path.realpath(os.path.join(data, "libreoffice"))
    with open(os.path.join(data, ".install.lock"), "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        with open(record, encoding="utf-8") as f:
            paths = [line.strip() for line in f if line.strip()]
        folders = set()
        for relative in paths:
            target = os.path.realpath(os.path.join(root, relative))
            if not target.startswith(root + os.sep):
                continue
            try:
                os.remove(target)
            except FileNotFoundError:
                pass
            folders.add(os.path.dirname(target))
        # Folders the pack made (resource/<code>, the dictionary's) go too once they're empty.
        for folder in sorted(folders, key=len, reverse=True):
            while folder.startswith(root + os.sep):
                try:
                    os.rmdir(folder)
                except OSError:
                    break
                folder = os.path.dirname(folder)
        os.remove(record)


def start():
    """Called from QML: install on a thread and report by event, so the screen stays responsive."""
    def run():
        try:
            install()
            _send("done")
        except InstallError as e:
            _send("failed", str(e))
        except Exception as e:  # anything unexpected still reaches the screen
            _send("failed", "Installation failed: %s" % e)
    threading.Thread(target=run, daemon=True).start()


if __name__ == "__main__":
    import sys
    if len(sys.argv) == 3 and sys.argv[1] in ("add", "remove"):
        (install_language if sys.argv[1] == "add" else remove_language)(sys.argv[2])
        print("languages:", installed_languages())
    else:
        install()
        print("installed:", installed())

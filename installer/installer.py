"""Download the official LibreOffice for arm64 and unpack it into the app's data folder.

The download is The Document Foundation's own build, checked against a fixed hash, so what runs
is exactly what TDF published. Nothing is installed system-wide and no root is needed.
Runs inside the installer screen (pyotherside); progress goes to QML as events.
"""
import fcntl
import hashlib
import io
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
USER_AGENT = "OfficeSuite-UbuntuTouch/0.1 (+https://github.com/juujbyg/office-suite-touch)"


class InstallError(Exception):
    pass


def _send(*args):
    if pyotherside:
        pyotherside.send(*args)
    else:
        print(*args)


def _data():
    return os.environ.get("OFFICE_DATA") or os.path.expanduser("~/.local/share/office.juujbyg")


def installed(data=None):
    opt = os.path.join(data or _data(), "libreoffice", "opt")
    if not os.path.isdir(opt):
        return False
    return any(os.path.exists(os.path.join(opt, n, "program", "soffice.bin")) for n in os.listdir(opt))


def _download(target):
    sha = hashlib.sha256()
    request = urllib.request.Request(URL, headers={"User-Agent": USER_AGENT})
    try:
        response = urllib.request.urlopen(request, timeout=30)
    except OSError as e:
        raise InstallError("Couldn't reach The Document Foundation's server. Check the connection. (%s)" % e)
    with response, open(target, "wb") as out:
        total = int(response.headers.get("Content-Length") or DOWNLOAD_MB << 20)
        done = 0
        while True:
            chunk = response.read(CHUNK)
            if not chunk:
                break
            out.write(chunk)
            sha.update(chunk)
            done += len(chunk)
            _send("progress", "download", min(done / total, 1.0))
    if sha.hexdigest() != SHA256:
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
    install()
    print("installed:", installed())

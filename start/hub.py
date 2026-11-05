"""Backend of the LibreOffice start screen (start/Main.qml, through pyotherside).

The app is confined, so LibreOffice only sees the app's own folders. Documents live in
~/.local/share/libreoffice.juujbyg/Documents: files shared to the app through Content Hub are copied
there, LibreOffice saves there, and the start screen sends them back out through Content Hub.
"""
import glob
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.parse

try:
    import pyotherside
except ImportError:  # tests on the PC
    pyotherside = None

ID = "libreoffice.juujbyg"
APP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"), ID)
DOCS = os.path.join(DATA, "Documents")
# The menu language picked on the Languages page (a pack's code), which the launcher passes to
# LibreOffice as --language. No file means LibreOffice follows the phone's language.
UI_LANGUAGE = os.path.join(DATA, "ui-language")
# LibreOffice's single-instance socket (see bin/office).
RUN = os.path.join(os.environ.get("XDG_RUNTIME_DIR") or "/run/user/%d" % os.getuid(), ID)
# What to open when LibreOffice is started through the URL dispatcher: the app, then the document.
REQUEST = os.path.join(DATA, "open-request")
REQUEST_SECONDS = 30

sys.path.insert(0, os.path.join(APP_DIR, "installer"))
import installer  # noqa: E402
import certs  # noqa: E402

KINDS = {
    "writer": (".odt", ".ott", ".fodt", ".doc", ".docx", ".dot", ".dotx", ".docm", ".rtf", ".txt", ".html", ".htm", ".xml", ".wpd", ".pages", ".epub"),
    "calc": (".ods", ".ots", ".fods", ".xls", ".xlsx", ".xlsm", ".xlt", ".xltx", ".csv", ".tsv", ".numbers", ".dbf"),
    "impress": (".odp", ".otp", ".fodp", ".ppt", ".pptx", ".pps", ".ppsx", ".pot", ".potx", ".key"),
    "draw": (".odg", ".otg", ".fodg", ".pdf", ".vsd", ".vsdx", ".svg", ".cdr", ".pub"),
    "math": (".odf", ".mml"),
    "base": (".odb",),
}
# LibreOffice keeps its lock files next to the document while it is open.
HIDDEN = (".~lock.", ".")


def kind_of(name):
    ext = os.path.splitext(name)[1].lower()
    for kind, extensions in KINDS.items():
        if ext in extensions:
            return kind
    return "other"


def _local_path(url):
    if url.startswith("file://"):
        return urllib.parse.unquote(urllib.parse.urlparse(url).path)
    return url


def _free_name(name):
    """A name in DOCS that isn't taken: 'Report.docx', then 'Report (2).docx' and so on."""
    base, ext = os.path.splitext(os.path.basename(name) or "Document")
    candidate, n = base + ext, 2
    while os.path.exists(os.path.join(DOCS, candidate)):
        candidate, n = "%s (%d)%s" % (base, n, ext), n + 1
    return candidate


def documents():
    """The documents in the app's folder, newest first."""
    os.makedirs(DOCS, exist_ok=True)
    found = []
    for entry in os.scandir(DOCS):
        if entry.name.startswith(HIDDEN) or not entry.is_file():
            continue
        st = entry.stat()
        found.append({"name": entry.name, "path": entry.path, "size": st.st_size,
                      "modified": st.st_mtime, "kind": kind_of(entry.name)})
    found.sort(key=lambda d: d["modified"], reverse=True)
    return found


def take(urls):
    """Copy files handed over by Content Hub into the app's folder. Returns the new paths."""
    os.makedirs(DOCS, exist_ok=True)
    paths = []
    for url in urls:
        source = _local_path(url)
        if not os.path.isfile(source):
            continue
        target = os.path.join(DOCS, _free_name(source))
        shutil.copyfile(source, target)
        paths.append(target)
    return paths


def _launch(args):
    subprocess.Popen([os.path.join(APP_DIR, "bin", "office")] + args, cwd=DOCS,
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def office_running():
    """Whether a LibreOffice is already running (its single-instance socket answers)."""
    for path in glob.glob(os.path.join(RUN, "OSL_PIPE_*SingleOfficeIPC_*")):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(1)
            try:
                s.connect(path)
                return True
            except OSError:
                pass
    return False


def _forget_request(content):
    time.sleep(REQUEST_SECONDS)
    try:
        with open(REQUEST, encoding="utf-8") as f:
            if f.read() == content:
                os.remove(REQUEST)
    except OSError:
        pass


def _open(app, path=""):
    """Open a document (or a new one) in LibreOffice. Returns "" when done here, or the address
    for QML to open (Qt.openUrlExternally).

    LibreOffice mustn't run inside the start screen's app: Lomiri gives the keyboard only to the
    focused app, and when one app has two windows it checks the first one (the start screen), so
    the keyboard never showed in LibreOffice. So the first LibreOffice is started as the suite's
    own app (Writer, Calc...) through the URL dispatcher, and bin/office picks up REQUEST. Once
    one is running, the document is passed to it, and its window belongs to that app."""
    if office_running():
        _launch([path] if path else [app])
        return ""
    content = "%s\n%s\n" % (app, path)
    with open(REQUEST + ".new", "w", encoding="utf-8") as f:
        f.write(content)
    os.rename(REQUEST + ".new", REQUEST)
    threading.Thread(target=_forget_request, args=(content,), daemon=True).start()
    return "appid://%s/%s/current-user-version" % (ID, app)


def open_document(path):
    path = os.path.realpath(path)
    if os.path.dirname(path) != os.path.realpath(DOCS) or not os.path.isfile(path):
        return None
    kind = kind_of(path)
    return _open(kind if kind in KINDS else "writer", path)


def new_document(kind):
    if kind not in KINDS:
        return None
    os.makedirs(DOCS, exist_ok=True)
    return _open(kind)


def delete_document(path):
    path = os.path.realpath(path)
    if os.path.dirname(path) != os.path.realpath(DOCS) or not os.path.isfile(path):
        return False
    os.remove(path)
    return True


# Languages page ------------------------------------------------------------------------------

def _send(*args):
    if pyotherside:
        pyotherside.send(*args)


def ui_language():
    try:
        with open(UI_LANGUAGE, encoding="utf-8") as f:
            return f.readline().strip()
    except OSError:
        return ""


def set_ui_language(code):
    if not code:
        try:
            os.remove(UI_LANGUAGE)
        except FileNotFoundError:
            pass
        return True
    if code not in installer.installed_languages(DATA):
        return False
    with open(UI_LANGUAGE + ".new", "w", encoding="utf-8") as f:
        f.write(code + "\n")
    os.rename(UI_LANGUAGE + ".new", UI_LANGUAGE)
    return True


def languages():
    """Everything the Languages page shows."""
    installed = set(installer.installed_languages(DATA))
    items = []
    for entry in installer.catalog():
        item = dict(entry)
        item.pop("sha256", None)
        item["installed"] = entry["code"] in installed
        items.append(item)
    return {"office": installer.installed(DATA), "phone": installer.phone_language() or "",
            "ui": ui_language(), "items": items}


def _run(code, job):
    def run():
        try:
            job()
            _send("language", code, "done", "")
        except installer.InstallError as e:
            _send("language", code, "failed", str(e))
        except Exception as e:  # noqa: BLE001  anything unexpected still reaches the screen
            _send("language", code, "failed", "Something went wrong: %s" % e)
    threading.Thread(target=run, daemon=True).start()


def add_language(code):
    """Download and add a language pack, on a thread: replies with a "language" event."""
    _run(code, lambda: installer.install_language(code, DATA))


def remove_language(code):
    def job():
        installer.remove_language(code, DATA)
        if ui_language() == code:
            set_ui_language("")
    _run(code, job)


# Certificates page (signing) -------------------------------------------------------------------

def is_certificate(url):
    return _local_path(url).lower().endswith(certs.EXTENSIONS)


def certificates():
    """Everything the Certificates page shows."""
    try:
        items, error = certs.list_certificates(DATA), ""
    except certs.CertError as e:
        items, error = [], str(e)
    return {"items": items, "error": error, "running": office_running()}


def add_certificate(url, password):
    """Import a .p12/.pfx file (from Content Hub), on a thread: replies with a "certificate" event.
    The file itself isn't kept: QML finalizes the transfer, which deletes the incoming copy."""
    path = _local_path(url)

    def run():
        try:
            items = certs.import_certificate(DATA, path, password)
            added = [c["name"] for c in items if c.get("new")]
            _send("certificate", "done", ", ".join(added))
        except certs.CertError as e:
            _send("certificate", "failed", str(e))
        except Exception as e:  # noqa: BLE001  anything unexpected still reaches the screen
            _send("certificate", "failed", "Something went wrong: %s" % e)
    threading.Thread(target=run, daemon=True).start()


def remove_certificate(cert_id):
    try:
        return "" if certs.remove_certificate(DATA, cert_id) else "That certificate is already gone."
    except certs.CertError as e:
        return str(e)

"""Backend of the LibreOffice start screen (start/Main.qml, through pyotherside).

The app is confined, so LibreOffice only sees the app's own folders. Documents live in
~/.local/share/libreoffice.juujbyg/Documents: files shared to the app through Content Hub are copied
there, LibreOffice saves there, and the start screen sends them back out through Content Hub.
"""
import os
import shutil
import subprocess
import urllib.parse

ID = "libreoffice.juujbyg"
APP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"), ID)
DOCS = os.path.join(DATA, "Documents")

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
    # The launcher sizes LibreOffice for the phone and, on first use, installs it. It runs in this
    # app's own process group, so Lomiri treats LibreOffice's window as this app's.
    subprocess.Popen([os.path.join(APP_DIR, "bin", "office")] + args, cwd=DOCS,
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def open_document(path):
    path = os.path.realpath(path)
    if os.path.dirname(path) != os.path.realpath(DOCS) or not os.path.isfile(path):
        return False
    _launch([path])
    return True


def new_document(kind):
    if kind not in KINDS:
        return False
    os.makedirs(DOCS, exist_ok=True)
    _launch([kind])
    return True


def delete_document(path):
    path = os.path.realpath(path)
    if os.path.dirname(path) != os.path.realpath(DOCS) or not os.path.isfile(path):
        return False
    os.remove(path)
    return True

"""PC check of start/hub.py (the start screen's backend), with a temporary data folder."""
import os, sys, tempfile
tmp = tempfile.mkdtemp()
os.environ["XDG_DATA_HOME"] = tmp
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "start"))
import hub

launched = []
hub._launch = launched.append
outside = os.path.join(tmp, "elsewhere"); os.makedirs(outside)
src = os.path.join(outside, "Report final.docx"); open(src, "w").write("x")

a = hub.take(["file://" + src.replace(" ", "%20")])
b = hub.take([src, "file:///nonexistent/x.odt"])
assert [os.path.basename(p) for p in a + b] == ["Report final.docx", "Report final (2).docx"], a + b
open(os.path.join(hub.DOCS, ".~lock.Report final.docx#"), "w").write("lock")
names = [d["name"] for d in hub.documents()]
assert sorted(names) == ["Report final (2).docx", "Report final.docx"] and hub.documents()[0]["kind"] == "writer", names
print("ok take: copies in, renames clashes, hides lock files")

hub.REQUEST_SECONDS = 0.3
hub.RUN = os.path.join(tmp, "run"); os.makedirs(hub.RUN)
assert not hub.office_running()
url = hub.open_document(a[0])
assert url == "appid://libreoffice.juujbyg/writer/current-user-version" and not launched, (url, launched)
assert open(hub.REQUEST).read() == "writer\n%s\n" % os.path.realpath(a[0])
assert hub.new_document("calc") == "appid://libreoffice.juujbyg/calc/current-user-version"
assert open(hub.REQUEST).read() == "calc\n\n"
import time; time.sleep(0.6)
assert not os.path.exists(hub.REQUEST), "an unused request is forgotten"
print("ok not running: starts the suite app through the URL dispatcher, with a one-time request")

import socket
open(os.path.join(hub.RUN, "OSL_PIPE_1_SingleOfficeIPC_stale"), "w").close()
assert not hub.office_running(), "a stale socket file isn't a running LibreOffice"
server = socket.socket(socket.AF_UNIX); server.bind(os.path.join(hub.RUN, "OSL_PIPE_1_SingleOfficeIPC_abc")); server.listen(16)
assert hub.office_running()
assert hub.open_document(a[0]) == "" and launched[-1] == [os.path.realpath(a[0])]
print("ok running: the document goes to the running LibreOffice")
assert hub.open_document(src) is None and hub.open_document(os.path.join(hub.DOCS, "..", "x")) is None
assert not hub.delete_document(src) and os.path.exists(src)
assert hub.delete_document(b[0]) and not os.path.exists(b[0])
assert hub.new_document("calc") == "" and launched[-1] == ["calc"] and hub.new_document("rm") is None
server.close()
print("ok open/delete only inside the documents folder; new only for known apps")
assert hub.kind_of("a.XLSX") == "calc" and hub.kind_of("a.pdf") == "draw" and hub.kind_of("a.zip") == "other"
print("ok kinds")

assert not hub.set_ui_language("es") and hub.ui_language() == ""
os.makedirs(os.path.join(hub.DATA, "libreoffice", "languages")); open(os.path.join(hub.DATA, "libreoffice", "languages", "pt-BR.files"), "w").close()
assert hub.set_ui_language("pt-BR") and open(hub.UI_LANGUAGE).read() == "pt-BR\n" and hub.ui_language() == "pt-BR"
info = hub.languages()
assert not info["office"] and info["ui"] == "pt-BR" and all("sha256" not in i for i in info["items"])
assert [i["code"] for i in info["items"] if i["installed"]] == ["pt-BR"]
assert hub.set_ui_language("") and hub.ui_language() == "" and not os.path.exists(hub.UI_LANGUAGE)
print("ok languages page backend")

open(hub.PICK_REQUEST, "w").write("calc\n")
assert hub.pick_request() == "calc" and hub.pick_request() == "", "a pick request is used once"
open(hub.PICK_REQUEST, "w").write("../../x\n")
assert hub.pick_request() == "" and not os.path.exists(hub.PICK_REQUEST)
open(hub.PICK_REQUEST, "w").write("writer\n"); old = time.time() - 600; os.utime(hub.PICK_REQUEST, (old, old))
assert hub.pick_request() == "", "an old request is ignored"
assert hub.picked("calc", []) == "appid://libreoffice.juujbyg/calc/current-user-version" and not os.path.exists(hub.PICKED)
assert hub.picked("calc", ["file://" + src]) == "appid://libreoffice.juujbyg/calc/current-user-version"
got = open(hub.PICKED).read()
assert got.startswith(hub.DOCS + "/Report final") and os.path.isfile(got.strip()), got
assert hub.picked("evil", ["file://" + src]) == ""
print("ok phone files for the Open dialog: one-time request, copy in, path left for the dialog")

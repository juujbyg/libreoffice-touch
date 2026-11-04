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

assert hub.open_document(a[0]) and launched[-1] == [os.path.realpath(a[0])]
assert not hub.open_document(src) and not hub.open_document(os.path.join(hub.DOCS, "..", "x"))
assert not hub.delete_document(src) and os.path.exists(src)
assert hub.delete_document(b[0]) and not os.path.exists(b[0])
assert hub.new_document("calc") and launched[-1] == ["calc"] and not hub.new_document("rm")
print("ok open/delete only inside the documents folder; new only for known apps")
assert hub.kind_of("a.XLSX") == "calc" and hub.kind_of("a.pdf") == "draw" and hub.kind_of("a.zip") == "other"
print("ok kinds")

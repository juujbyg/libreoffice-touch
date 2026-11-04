"""Phone defaults in the LibreOffice profile (run with LibreOffice closed): large toolbar and
sidebar icons, and Writer opening documents at page width."""
import re, sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
items = {
    ("/org.openoffice.Office.Common/Misc", "SymbolSet"): ("xs:short", "1"),
    ("/org.openoffice.Office.Common/Misc", "SidebarIconSize"): ("xs:short", "2"),
    ("/org.openoffice.Office.Writer/Layout/Zoom", "Type"): ("xs:int", "3"),
}
for (path, name), (typ, val) in items.items():
    pat = re.compile(r'<item oor:path="%s"><prop oor:name="%s"[^>]*>.*?</item>\n?' % (re.escape(path), name), re.S)
    s = pat.sub("", s)
    s = s.replace("</oor:items>", '<item oor:path="%s"><prop oor:name="%s" oor:op="fuse"><value>%s</value></prop></item>\n</oor:items>' % (path, name, val))
open(p, "w", encoding="utf-8").write(s)
print("ok")

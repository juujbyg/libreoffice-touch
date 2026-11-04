"""Hide the slide/page list in Impress and Draw by default (View > Slide Pane brings it back)."""
import re, sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
for name in ("ImpressView", "DrawView"):
    path = "/org.openoffice.Office.Impress/MultiPaneGUI/SlideSorterBar/Visible"
    s = re.sub(r'<item oor:path="%s"><prop oor:name="%s"[^>]*>.*?</item>\n?' % (re.escape(path), name), "", s, flags=re.S)
    s = s.replace("</oor:items>", '<item oor:path="%s"><prop oor:name="%s" oor:op="fuse"><value>false</value></prop></item>\n</oor:items>' % (path, name))
open(p, "w", encoding="utf-8").write(s)
print("ok")

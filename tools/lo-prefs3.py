"""Collapse the sidebar to its icon strip in Draw, Impress and Math, as Writer and Calc already are
(an open deck took 760 of the 1080 px). Run with LibreOffice closed."""
import re, sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
for app in ("sdraw", "simpress", "smath"):
    s, n = re.subn(r"(WindowType\['%s/10336'\]/UserData\"><prop oor:name=\"Data\"[^>]*><value>)[^<]*(</value>)" % app,
                   r"\g<1>V2,V,0,AL:(5,16,0/0/48/450,48;450)\g<2>", s)
    print(app, n)
open(p, "w", encoding="utf-8").write(s)

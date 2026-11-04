"""Install the PhoneView macro (Writer files open at page width) and bind it to Open Document.
Also drops the Load/UserDefinedSettings=false tried before (it did not change the zoom).
Usage: lo-prefs5.py <profile user dir> <PhoneView.xba>. Run with LibreOffice closed."""
import os, re, shutil, sys
user, xba = sys.argv[1], sys.argv[2]
std = os.path.join(user, "basic", "Standard")
shutil.copy(xba, os.path.join(std, "PhoneView.xba"))
xlb = os.path.join(std, "script.xlb")
s = open(xlb, encoding="utf-8").read()
if 'library:name="PhoneView"' not in s:
    s = s.replace("</library:library>", ' <library:element library:name="PhoneView"/>\n</library:library>')
    open(xlb, "w", encoding="utf-8").write(s)
p = os.path.join(user, "registrymodifications.xcu")
s = open(p, encoding="utf-8").read()
s = re.sub(r'<item oor:path="/org.openoffice.Office.Common/Load"><prop oor:name="UserDefinedSettings"[^>]*>.*?</item>\n?', "", s, flags=re.S)
s = re.sub(r'<item oor:path="/org.openoffice.Office.Events/ApplicationEvents/Bindings"><node oor:name="OnLoad".*?</item>\n?', "", s, flags=re.S)
url = "vnd.sun.star.script:Standard.PhoneView.FitWidth?language=Basic&amp;location=application"
s = s.replace("</oor:items>", '<item oor:path="/org.openoffice.Office.Events/ApplicationEvents/Bindings"><node oor:name="OnLoad" oor:op="replace"><prop oor:name="BindingURL" oor:op="fuse"><value>%s</value></prop></node></item>\n</oor:items>' % url)
open(p, "w", encoding="utf-8").write(s)
print("ok")

#!/bin/sh
# Root, on the phone: LibreOffice's Open dialog (native/noparent.c, "Open and Save dialogs"). Starts
# Writer with a throwaway profile and data folder (LibreOffice itself linked in from the real one),
# opens File > Open from a macro, and lets the shim's test mode (LO_SHIM_TEST_DIALOG) draw the dialog
# into $T/open.png, press "Phone files…" (without bringing up the start screen), select
# "Picked test.txt" as if it had come back from the start screen, draw $T/open.png.after.png and cancel.
# With "save" as the third argument it opens File > Save As instead, draws it into
# $T/save.png.after.png and cancels.
# Usage: dialog-test.sh <version> [seconds before the press, default 12] [open|save]
V=$1; WAIT=${2:-12}; KIND=${3:-open}; case "$KIND" in save) CMD=.uno:SaveAs;; *) KIND=open; CMD=.uno:Open;; esac; ID=libreoffice.juujbyg; H=/home/phablet; PROF=${ID}_writer_$V
APP=/opt/click.ubuntu.com/$ID/current; T=$H/.cache/$ID/dialogtest; D=$T/data/$ID
ENV="export XDG_RUNTIME_DIR=/run/user/32011 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus"
for e in /proc/[0-9]*/exe; do case "$(readlink $e 2>/dev/null)" in */$ID/*soffice.bin) echo "LibreOffice is open: not testing"; exit 1;; esac; done
c=$(dbus-send --system --print-reply --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.keepDisplayOn | awk '/int32/{print $2}')
rm -rf $T; mkdir -p $D/Documents
ln -s $H/.local/share/$ID/libreoffice $D/libreoffice
echo "picked" > "$D/Documents/Picked test.txt"; echo "other" > "$D/Documents/Another.txt"
chown -R phablet: $T
# run "<settings and timeout>" <office arguments...>
run() { pre=$1; shift; su phablet -c "$ENV; cd $T; APP_ID=$PROF XDG_DATA_HOME=$T/data XDG_CONFIG_HOME=$T/config XDG_CACHE_HOME=$H/.cache $pre aa-exec -p $PROF -- $APP/bin/office $*"; }
echo "1. profile"; run "timeout 150" --headless --terminate_after_init >/dev/null 2>&1; echo "exit $?"
B=$T/config/$ID/profile/user/basic/Standard
# The document's own frame: StarDesktop.CurrentFrame is empty when Writer isn't the focused app,
# and the dispatch then goes nowhere. Started with a macro, LibreOffice opens no document, so the
# macro makes a new one and uses its frame. It quits LibreOffice only after the shim's test steps
# (press at $WAIT s, pick 1 s later, cancel 2 s after that), whether or not the dispatch waits for
# the dialog. Each step goes to $T/macro.log, and an error quits too: LibreOffice's own error box is
# invisible to the shim and would hang the test.
cat > $B/DialogTest.xba <<X
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE script:module PUBLIC "-//OpenOffice.org//DTD OfficeDocument 1.0//EN" "module.dtd">
<script:module xmlns:script="http://openoffice.org/2000/script" script:name="DialogTest" script:language="StarBasic" script:moduleType="normal">Sub Note(s)
	Dim n
	n = FreeFile
	Open &quot;$T/macro.log&quot; For Append As #n
	Print #n, Format(Now, &quot;hh:mm:ss&quot;) &amp; &quot; &quot; &amp; s
	Close #n
End Sub

Sub Main
	On Error GoTo Failed
	Dim c, f
	Note &quot;start&quot;
	Wait 3000
	c = StarDesktop.loadComponentFromURL(&quot;private:factory/swriter&quot;, &quot;_blank&quot;, 0, Array())
	f = c.CurrentController.Frame
	Note &quot;new document open&quot;
	Wait 3000
	CreateUnoService(&quot;com.sun.star.frame.DispatchHelper&quot;).executeDispatch(f, &quot;$CMD&quot;, &quot;&quot;, 0, Array())
	Note &quot;dispatch returned&quot;
	Wait $(( (WAIT + 8) * 1000 ))
	Note &quot;quit&quot;
	StarDesktop.terminate()
	Exit Sub
Failed:
	Note &quot;error &quot; &amp; Err &amp; &quot; at line &quot; &amp; Erl &amp; &quot;: &quot; &amp; Error\$
	StarDesktop.terminate()
End Sub
</script:module>
X
sed -i 's#</library:library># <library:element library:name="DialogTest"/>\n</library:library>#' $B/script.xlb
chown -R phablet: $T
echo "2. $KIND dialog"; S=$(date +%s)
run "LO_SHIM_DEBUG=1 LO_SHIM_TEST_DIALOG=$WAIT LO_SHIM_SNAPSHOT=$T/$KIND.png LO_SHIM_TEST_PICKED='$D/Documents/Picked test.txt' timeout 150" writer macro:///Standard.DialogTest.Main
echo "office exited with $? after $(( $(date +%s) - S )) s"
echo "--- macro:"; cat $T/macro.log 2>&1
echo "--- shim:"; grep "lo-shim: \(phone\|test dialog\|snapshot\|show\)" $H/.cache/$ID/last.log
ls $D; ls -la $T/*.png 2>&1
echo "--- denials:"; dmesg | grep DENIED | grep "$PROF" | grep -v "qtshader\|loginuid\|mountinfo\|/mounts" | sed -E 's/.*apparmor="DENIED"//; s/ (pid|fsuid|ouid)=[0-9]+//g' | sort | uniq -c | tail -10 | cut -c1-200
dbus-send --system --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.removeDisplayOnRequest int32:$c
echo END

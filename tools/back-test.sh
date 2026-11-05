#!/bin/sh
# Root, on the phone: the back arrow (native/noparent.c). Starts Writer with a new document and a
# throwaway profile; the shim draws the window into $T/writer.png and then presses the arrow by
# itself. A new, unchanged document closes without asking, so LibreOffice should quit and the start
# screen come up. Usage: back-test.sh <version> [seconds before the press, default 25]
V=$1; WAIT=${2:-25}; ID=libreoffice.juujbyg; H=/home/phablet; PROF=${ID}_writer_$V
APP=/opt/click.ubuntu.com/$ID/current; T=$H/.cache/$ID/backtest
ENV="export XDG_RUNTIME_DIR=/run/user/32011 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus"
for e in /proc/[0-9]*/exe; do case "$(readlink $e 2>/dev/null)" in */$ID/*soffice.bin) echo "LibreOffice is open: not testing"; exit 1;; esac; done
c=$(dbus-send --system --print-reply --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.keepDisplayOn | awk '/int32/{print $2}')
rm -rf $T; mkdir -p $T; chown phablet: $T
S=$(date +%s); since=$(date '+%F %T')
su phablet -c "$ENV; cd $T; APP_ID=$PROF LO_SHIM_DEBUG=1 LO_SHIM_TEST_BACK=$WAIT LO_SHIM_SNAPSHOT=$T/writer.png XDG_CONFIG_HOME=$T/config timeout 180 aa-exec -p $PROF -- $APP/bin/office writer"
echo "office exited with $? after $(( $(date +%s) - S )) s"
grep "lo-shim: \(back\|start screen\|snapshot\)" $H/.cache/$ID/last.log
ls -la $T/writer.png 2>&1
sleep 3
echo "--- launches:"; su phablet -c "$ENV; journalctl --user --since '$since' --no-pager" | grep -E "Started lomiri-app-launch--application-click--$ID|URL|url-disp" | cut -c1-170
echo "--- denials:"; dmesg | grep DENIED | grep "$PROF" | grep -v "qtshader\|loginuid" | sed -E 's/.*apparmor="DENIED"//; s/ (pid|fsuid|ouid)=[0-9]+//g' | sort | uniq -c | tail -8 | cut -c1-200
dbus-send --system --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.removeDisplayOnRequest int32:$c
echo END

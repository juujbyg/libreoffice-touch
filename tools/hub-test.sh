#!/bin/sh
# Root, on the phone: install the click, open the start screen, then share Report.docx to LibreOffice
# from Document Viewer's sandbox (tools/share-test.qml). Screens: /tmp/hub-start.raw, /tmp/hub-share.raw.
# Usage: hub-test.sh <click> <version>
CLICK=$1; V=$2; H=/home/phablet; ID=libreoffice.juujbyg
ENV="export XDG_RUNTIME_DIR=/run/user/32011 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus"
ours() { for e in /proc/[0-9]*/exe; do case "$(readlink $e 2>/dev/null)" in */$ID/*) p=${e#/proc/}; echo ${p%/exe};; esac; done; pgrep -f "[q]mlscene.*$ID"; }
shot() { for i in 1 2; do timeout 20 mirscreencast -m /run/user/32011/mir_socket_trusted -n 1 -f /tmp/hub-$1.raw >/dev/null 2>&1; sleep 1; done; [ -s /tmp/hub-$1.raw ] && echo "shot $1" || echo "NO SHOT $1 (screen locked?)"; }
for p in $(ours); do kill -9 $p; done
cp "$CLICK" $H/ && chown phablet: "$H/${CLICK##*/}"
su phablet -c "gdbus call --system --dest com.lomiri.click --object-path /com/lomiri/click --method com.lomiri.click.Install $H/${CLICK##*/}" >/dev/null 2>&1
sleep 4; click list --user=phablet | grep $ID; rm -f "$H/${CLICK##*/}"
rm -f /tmp/hub-*.raw
c=$(dbus-send --system --print-reply --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.keepDisplayOn | awk '/int32/{print $2}')
T=$(date '+%F %T')
su phablet -c "$ENV; timeout 10 lomiri-app-launch ${ID}_a-libreoffice_$V"
sleep 12; shot start
D=$H/.cache/docviewer.ubports; cp /tmp/share-test.qml "$H/Documents/LibreOffice test/Report.docx" $D/ && chown phablet: $D/share-test.qml $D/Report.docx
su phablet -c "$ENV; cd $D; APP_ID=docviewer.ubports_docviewer_3.1.3 QT_QPA_PLATFORM=offscreen aa-exec -p docviewer.ubports_docviewer_3.1.3 -- qmlscene $D/share-test.qml" 2>&1 | grep -E "peer|shared|offered" | head
sleep 35; shot share
echo "--- documents:"; ls -la $H/.local/share/$ID/Documents/
echo "--- running:"; for p in $(ours); do echo "$p $(cat /proc/$p/attr/current) $(tr '\0' ' ' < /proc/$p/cmdline | cut -c1-160)"; done
echo "--- denials:"; dmesg | grep DENIED | grep -E "$ID|docviewer" | sed -E 's/.*apparmor="DENIED"//; s/ (pid|fsuid|ouid)=[0-9]+//g' | sort | uniq -c | tail -15 | cut -c1-220
dbus-send --system --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.removeDisplayOnRequest int32:$c
echo END

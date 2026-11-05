#!/bin/sh
# Root, on the phone: open a new Writer document the normal way with the keyboard trace on
# (native/noparent.c), leave it for <seconds>, then close it. Trace: /tmp/im-trace.log
# Usage: im-trace.sh <version> [seconds]
V=$1; S=${2:-25}; ID=libreoffice.juujbyg; H=/home/phablet; C=$H/.cache/$ID
ENV="export XDG_RUNTIME_DIR=/run/user/32011 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus"
c=$(dbus-send --system --print-reply --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.keepDisplayOn | awk '/int32/{print $2}')
for p in $(pgrep -f "[s]office.bin"); do kill -9 $p; done
touch $C/im-trace; chown phablet: $C/im-trace
su phablet -c "$ENV; timeout 10 lomiri-app-launch ${ID}_writer_$V"
sleep $S
grep -E '^im |maliit|Maliit' $C/last.log > /tmp/im-trace.log
for p in $(pgrep -f "[s]office.bin"); do kill -9 $p; done
rm -f $C/im-trace
dbus-send --system --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.removeDisplayOnRequest int32:$c
echo END

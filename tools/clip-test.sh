#!/bin/sh
# Root, on the phone, with the phone unlocked: the clipboard bridge in native/noparent.c.
# 1. Puts "hubtolo <nonce>" on Content Hub's clipboard, as a Qt app (Teleports) would.
# 2. Starts Writer the normal way (lomiri-app-launch), presses Ctrl+V and types " back".
# 3. Selects all and copies, then reads Content Hub's clipboard: it should say "hubtolo <nonce> back".
# 4. Undoes everything and quits with Ctrl+Q, so no unsaved document or recovery entry is left.
# The shim's trace and a snapshot of the window ($T/snap-*.png) show what happened in between.
# Needs tools/keys.py and tools/hubclip.py next to it.
# Usage: clip-test.sh <version>
V=$1; ID=libreoffice.juujbyg; H=/home/phablet; C=$H/.cache/$ID; HERE=$(cd "$(dirname "$0")" && pwd)
ENV="export XDG_RUNTIME_DIR=/run/user/32011 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus"
as_phablet() { su phablet -c "$ENV; $*"; }
office_pid() { for e in /proc/[0-9]*/exe; do case "$(readlink $e 2>/dev/null)" in */$ID/*soffice.bin) p=${e#/proc/}; echo ${p%/exe}; return;; esac; done; }
[ -n "$(office_pid)" ] && { echo "LibreOffice is open: not testing"; exit 1; }
c=$(dbus-send --system --print-reply --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.keepDisplayOn | awk '/int32/{print $2}')
had_trace=0; [ -e $C/im-trace ] && had_trace=1
touch $C/im-trace; rm -f $C/snap-*.png; chown phablet: $C/im-trace
N=$(od -An -N3 -tu4 /dev/urandom | tr -d ' ')
WORD="hubtolo $N"

echo "1. Content Hub clipboard: $WORD"
as_phablet "python3 -I $HERE/hubclip.py put '$WORD'" >/dev/null
echo "   reads back: $(as_phablet "python3 -I $HERE/hubclip.py get")"

echo "2. Writer"
as_phablet "timeout 10 lomiri-app-launch ${ID}_writer_$V" </dev/null >/dev/null 2>&1
for i in $(seq 60); do [ -n "$(office_pid)" ] && grep -q "clipboard:" $C/last.log 2>/dev/null && break; sleep 1; done
sleep 6
python3 -I $HERE/keys.py ctrl+v " back"
sleep 1; touch $C/snap-request; chown phablet: $C/snap-request; sleep 2

echo "3. copy from LibreOffice"
python3 -I $HERE/keys.py ctrl+a ctrl+c
sleep 2
GOT=$(as_phablet "python3 -I $HERE/hubclip.py get")
echo "   Content Hub now: $GOT"

echo "4. clean up"
python3 -I $HERE/keys.py ctrl+z ctrl+z ctrl+z ctrl+z ctrl+z ctrl+z ctrl+z ctrl+z ctrl+q
for i in $(seq 8); do [ -z "$(office_pid)" ] && break; sleep 1; done
# Undo doesn't clear the modified flag, so Ctrl+Q asks to save: "Don't Save" is Alt+D.
[ -n "$(office_pid)" ] && python3 -I $HERE/keys.py alt+d
for i in $(seq 15); do [ -z "$(office_pid)" ] && break; sleep 1; done
[ -n "$(office_pid)" ] && echo "   LibreOffice still running (pid $(office_pid))" || echo "   LibreOffice closed"
[ $had_trace = 1 ] || rm -f $C/im-trace
dbus-send --system --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.removeDisplayOnRequest int32:$c

echo "--- shim:"; grep "clipboard" $C/last.log | tail -8
ls $C/snap-*.png 2>/dev/null
# Writer capitalises the first letter of a sentence (AutoCorrect), so compare without case.
low() { printf %s "$1" | tr A-Z a-z; }
if [ "$(low "$GOT")" = "$(low "$WORD back")" ]; then echo "RESULT PASS"; else echo "RESULT FAIL"; fi

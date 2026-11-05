#!/bin/sh
# Root, on the phone: test language packs inside the app's sandbox, with a throwaway LibreOffice
# profile (the user's own profile isn't touched). Adds the pack <code> (default de), checks that
# LibreOffice's menus switch to it and its dictionary registers in a profile made before the pack
# was added, then removes it. Usage: lang-test.sh <version> [code]
V=$1; CODE=${2:-de}; ID=libreoffice.juujbyg; H=/home/phablet
PROF=${ID}_a-libreoffice_$V; APP=/opt/click.ubuntu.com/$ID/current
DATA=$H/.local/share/$ID; T=$H/.cache/$ID/langtest
ENV="export XDG_RUNTIME_DIR=/run/user/32011 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus"
c=$(dbus-send --system --print-reply --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.keepDisplayOn | awk '/int32/{print $2}')
sandbox() { su phablet -c "$ENV; cd $H; aa-exec -p $PROF -- $*"; }
# One headless LibreOffice start with the test profile; prints the menu language it chose.
office() {
  su phablet -c "$ENV; $1 XDG_CONFIG_HOME=$T timeout 120 aa-exec -p $PROF -- $APP/bin/office --headless --terminate_after_init" >/dev/null 2>&1
  echo "  rc=$? menus: $(grep -o 'ooLocale" oor:op="fuse"><value>[^<]*' $T/$ID/profile/user/registrymodifications.xcu | sed 's/.*<value>//')"
}
py() { sandbox python3 -I -c "\"import sys; sys.path.insert(0, '$APP/start'); import hub; $1\""; }

rm -rf $T; mkdir -p $T; chown phablet: $T
echo "1. fresh profile, phone in English"; office "LANG=en_US.UTF-8"
echo "2. add $CODE in the sandbox"
sandbox python3 -I $APP/installer/installer.py add $CODE 2>&1 | grep -v '^progress' | tail -3
ls -d $DATA/libreoffice/opt/libreoffice26.8/program/resource/$CODE $DATA/libreoffice/opt/libreoffice26.8/share/extensions/dict-$CODE 2>&1
echo "3. phone in English, nothing picked: still English"; office "LANG=en_US.UTF-8"
echo "4. picked on the Languages page"; py "print(hub.set_ui_language('$CODE'))"; cat $DATA/ui-language
office "LANG=en_US.UTF-8"
echo "   dictionary registered in the old profile: $(grep -rl "dict-$CODE" $T/$ID/profile/user/extensions/bundled 2>/dev/null | wc -l) files"
echo "5. nothing picked, phone in that language"; py "print(hub.set_ui_language(''))"
office "LANG=${CODE}_$(echo $CODE | tr a-z A-Z).UTF-8"
echo "6. languages() in the sandbox"
py "i = hub.languages(); print(i['office'], repr(i['phone']), repr(i['ui']), [x['code'] for x in i['items'] if x['installed']], len(i['items']))"
echo "7. remove $CODE"
py "hub.set_ui_language('$CODE')"
sandbox python3 -I -c "\"import sys; sys.path.insert(0, '$APP/start'); import hub, installer; installer.remove_language('$CODE', hub.DATA); hub.set_ui_language('') if hub.ui_language() == '$CODE' else None; print(installer.installed_languages(hub.DATA))\""
ls $DATA/libreoffice/opt/libreoffice26.8/program/resource/ $DATA/libreoffice/opt/libreoffice26.8/share/extensions/; ls $DATA/ui-language 2>&1 | tail -1
office "LANG=en_US.UTF-8"
echo "   dictionary entries left in the profile: $(grep -rl "dict-$CODE" $T/$ID/profile/user/extensions/bundled 2>/dev/null | wc -l)"
echo "--- denials:"; dmesg | grep DENIED | grep "$PROF" | sed -E 's/.*apparmor="DENIED"//; s/ (pid|fsuid|ouid)=[0-9]+//g' | sort | uniq -c | tail -12 | cut -c1-200
rm -rf $T
dbus-send --system --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.removeDisplayOnRequest int32:$c
echo END

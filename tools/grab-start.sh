#!/bin/sh
# root: render the start screen inside the app's sandbox with grabToImage (mirscreencast can stop working). Needs /tmp/grab.qml = tools/grab-start.qml
pkill -f '[w]ait-shot'; D=/home/phablet/.cache/libreoffice.juujbyg
cp /tmp/grab.qml $D/ && chown phablet: $D/grab.qml; rm -f $D/start.png
su phablet -c "export XDG_RUNTIME_DIR=/run/user/32011 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus APP_ID=libreoffice.juujbyg_z-libreoffice_0.2.3 QT_QPA_PLATFORM=ubuntumirclient; cd $D; aa-exec -p libreoffice.juujbyg_z-libreoffice_0.2.3 -- qmlscene $D/grab.qml" > /tmp/grab.log 2>&1 &
sleep 14; pkill -f '[q]mlscene.*grab.qml'; ls -la $D/start.png; grep -vE "dconf|^$" /tmp/grab.log | tail -8

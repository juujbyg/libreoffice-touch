#!/bin/sh
# Root, on the phone: test signing certificates in the app's sandbox. Needs test.p12 (password
# "pässword 1", subject CN=Test Signer, made by tests/certs_check.py's recipe) and note.txt in $T.
# 1. adds the certificate through the start screen's backend (tools/cert-test.qml) and grabs the page,
# 2. exports note.txt as a signed PDF with the installed bin/office (throwaway LibreOffice profile),
# 3. removes the certificate again. Check $T/note.pdf on the PC with pdfsig.
# Usage: cert-test.sh <version> "<certificate subject>"
V=$1; SUBJ=$2; ID=libreoffice.juujbyg; H=/home/phablet; PROF=${ID}_a-libreoffice_$V
APP=/opt/click.ubuntu.com/$ID/current; T=$H/.cache/$ID/certtest
ENV="export XDG_RUNTIME_DIR=/run/user/32011 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus"
c=$(dbus-send --system --print-reply --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.keepDisplayOn | awk '/int32/{print $2}')
chown -R phablet: $T; rm -f $T/certificates.png $T/note.pdf
qml() {
  su phablet -c "$ENV; cd $T; CERT_TEST_MODE=$1 APP_ID=${ID}_a-libreoffice_$V QT_QPA_PLATFORM=ubuntumirclient timeout 120 aa-exec -p $PROF -- qmlscene $T/cert-test.qml" 2>&1 | grep "cert-test:"
}
echo "1. add through the backend"; qml add
ls -la $H/.local/share/$ID/certificates/
echo "2. signed PDF with bin/office"
printf 'pdf:writer_pdf_Export:{"SignPDF":{"type":"boolean","value":"true"},"SignCertificateSubjectName":{"type":"string","value":"%s"}}\n' "$SUBJ" > $T/opts
chown phablet: $T/opts
for try in 1 2; do
  su phablet -c "$ENV; cd $T; XDG_CONFIG_HOME=$T/config timeout 150 aa-exec -p $PROF -- $APP/bin/office --headless --convert-to \"\$(cat $T/opts)\" --outdir $T $T/note.txt" 2>&1 | grep -v "^$" | tail -1
  [ -s $T/note.pdf ] && break
done
ls -la $T/note.pdf
echo "3. remove through the backend"; qml remove
echo "--- denials:"; dmesg | grep DENIED | grep "$PROF" | sed -E 's/.*apparmor="DENIED"//; s/ (pid|fsuid|ouid)=[0-9]+//g; s#/proc/[0-9]+/#/proc/N/#' | sort | uniq -c | tail -12 | cut -c1-200
rm -rf $T/config
dbus-send --system --dest=com.canonical.Unity.Screen /com/canonical/Unity/Screen com.canonical.Unity.Screen.removeDisplayOnRequest int32:$c
echo END

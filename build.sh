#!/bin/sh
# Builds libreoffice.juujbyg_<version>_arm64.click in build/.
# Needs Docker (the clickable 24.04 arm64 image has the cross compiler and click).
# LibreOffice itself isn't in the click: the app downloads it from The Document Foundation
# on first use (installer/).
set -eu
cd "$(dirname "$0")"
IMAGE=clickable/amd64-ut24.04-1.x-arm64:latest
UBUNTU=http://ports.ubuntu.com/ubuntu-ports/pool/universe/m
# Lomiri's on-screen keyboard for GTK apps (LGPL-2.1+), from Ubuntu's archive, checked by hash.
MALIIT_GLIB=libmaliit-glib2_2.3.0-4build5_arm64.deb
MALIIT_GLIB_SHA=9926be018afe677b4a0bc450de4dcc550392947a71fce686ca9e4529b34067f3
MALIIT_GTK=maliit-inputcontext-gtk3_0.99.1+git20260515+faf373b-1_arm64.deb
MALIIT_GTK_SHA=e4da9e5cb791fc2711160a83e509c8985cdae2f3f151ab1287b8acf388c790a5

mkdir -p build/dl
fetch() {
  [ -f "build/dl/$1" ] || curl -fsSL -o "build/dl/$1" "$2/$1"
  echo "$3  build/dl/$1" | sha256sum -c --quiet
}
fetch "$MALIIT_GLIB" "$UBUNTU/maliit-framework" "$MALIIT_GLIB_SHA"
fetch "$MALIIT_GTK" "$UBUNTU/maliit-inputcontext-gtk" "$MALIIT_GTK_SHA"

rm -rf build/click
mkdir -p build/click
cp -r manifest.json office.apparmor libreoffice.content-hub LICENSE NOTICE apps bin icons installer profile share start build/click/
rm -rf build/click/installer/__pycache__ build/click/start/__pycache__

docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src "$IMAGE" sh -euc '
  lib=build/click/lib
  mkdir -p $lib/gtk-3.0/immodules build/deb
  aarch64-linux-gnu-gcc -O2 -Wall -shared -fPIC -o $lib/libnoparent.so native/noparent.c -ldl
  aarch64-linux-gnu-gcc -O2 -Wall -o build/click/bin/office-util native/office-util.c
  for d in build/dl/*.deb; do dpkg-deb -x "$d" build/deb; done
  a=build/deb/usr/lib/aarch64-linux-gnu
  cp -P $a/libmaliit-glib.so.2* $lib/
  cp $a/gtk-3.0/3.0.0/immodules/im-maliit.so $lib/gtk-3.0/immodules/
  rm -rf build/deb
  cd build && click build click
'
ls -l build/*.click

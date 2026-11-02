#!/bin/bash
set -e

DESTDIR=$(readlink -f appdir) ninja install

# linuxdeploy (the maintained successor of linuxdeployqt) is the only deployer
# that understands Qt 6; the old linuxdeployqt continuous builds are Qt 5 only.
wget -c -q "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
wget -c -q "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
chmod a+x linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage

# The qt plugin locates Qt through qmake; Qt 6 installs it as qmake6.
export QMAKE="$(command -v qmake6 || echo /usr/lib/qt6/bin/qmake6)"
export VERSION="$KLOGG_VERSION"
export EXTRA_QT_MODULES="core5compat"

./linuxdeploy-x86_64.AppImage \
    --appdir appdir \
    --desktop-file appdir/usr/share/applications/klogg.desktop \
    --plugin qt \
    --output appimage

mkdir -p ./packages
APPIMAGE=$(ls -1 ./*.AppImage | grep -v '^./linuxdeploy' | head -n 1)
cp "$APPIMAGE" "./packages/klogg-$KLOGG_VERSION-x86_64.AppImage"

#!/bin/bash
# (BOB) EndTimes release extras, as 0.14 shipped them: a win64 zip, a Linux daemon-only
# tarball and an x86_64 AppImage, from the SAME binaries as the installer and the Linux
# tarball (no rebuild). Run on the build host:
#   package_extras.sh <win-build-tree> <linux-tarball> <version> <outdir>
set -euo pipefail
WIN=$1 LINTAR=$2 V=$3 OUT=$4
N=dobbscoin-$V
T=$(mktemp -d); trap 'rm -rf $T' EXIT
mkdir -p $OUT

# win64 zip: the five programs, stripped, plus COPYING
mkdir -p $T/win/$N/bin
for b in dobbscoin-qt dobbscoind dobbscoin-cli dobbscoin-tx dobbscoin-wallet; do
  src=$WIN/src/$b.exe; [ $b = dobbscoin-qt ] && src=$WIN/src/qt/$b.exe
  x86_64-w64-mingw32-strip -o $T/win/$N/bin/$b.exe $src
done
cp $WIN/COPYING $T/win/$N/
(cd $T/win && TZ=UTC zip -q -X -r $OUT/dobbscoin-v$V-win64.zip $N)

# Linux daemon-only tarball: the tarball minus dobbscoin-qt
mkdir -p $T/lin && tar -xzf $LINTAR -C $T/lin
D=$(ls $T/lin)
mkdir -p $T/dmn/$D && cp -a $T/lin/$D/. $T/dmn/$D/ && rm -f $T/dmn/$D/bin/dobbscoin-qt
(cd $T/dmn && tar --owner=0 --group=0 --numeric-owner -czf $OUT/dobbscoin-v$V-x86_64-linux-gnu-daemon.tar.gz $D)

[ "${SKIP_APPIMAGE:-0}" = 1 ] && { ls -la $OUT | grep -E "win64.zip|daemon.tar.gz"; exit 0; }
# AppImage: Qt is linked statically by depends, so plain linuxdeploy (no qt plugin)
mkdir -p $T/tools
curl -fsSL -o $T/tools/linuxdeploy https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
chmod +x $T/tools/linuxdeploy
A=$T/AppDir; mkdir -p $A/usr/bin $A/usr/share/applications $A/usr/share/icons/hicolor/256x256/apps
install -m 0755 $T/lin/$D/bin/dobbscoin-qt $A/usr/bin/dobbscoin-qt
cat > $A/usr/share/applications/dobbscoin-qt.desktop <<'EOF'
[Desktop Entry]
Name=Dobbscoin
Comment=Dobbscoin (BOB) wallet, EndTimes
Exec=dobbscoin-qt %u
Terminal=false
Type=Application
Icon=dobbscoin
MimeType=x-scheme-handler/dobbscoin;
Categories=Office;Finance;
EOF
cp $WIN/share/pixmaps/bitcoin256.png $A/usr/share/icons/hicolor/256x256/apps/dobbscoin.png
(cd $T && APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 VERSION=$V ./tools/linuxdeploy --appdir $A \
   --executable $A/usr/bin/dobbscoin-qt --desktop-file $A/usr/share/applications/dobbscoin-qt.desktop \
   --icon-file $A/usr/share/icons/hicolor/256x256/apps/dobbscoin.png --output appimage > $T/linuxdeploy.log 2>&1) \
   || { tail -20 $T/linuxdeploy.log; exit 1; }
mv $T/Dobbscoin-$V-x86_64.AppImage $OUT/dobbscoin-v$V-x86_64.AppImage
ls -la $OUT | grep -E "win64.zip|daemon.tar.gz|AppImage"

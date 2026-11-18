#!/bin/bash
# EndTimes 0.21.2 beta1, x86_64 Linux tarball from depends (portable: Ubuntu 22.04 glibc).
# Same source as win64 v10 (729a77f48). Runs under MemoryHigh, low CPU weight (vps3 is production).
set -euo pipefail
W=~/claude/endtimes-win; L=~/claude/endtimes-linux; H=x86_64-pc-linux-gnu
CAP="systemd-run --user --scope -p MemoryHigh=2500M -p CPUWeight=20 --slice=background.slice nice -n 10"
step(){ echo "[$(date -u +%H:%M:%S)] $*"; }
rm -rf $L && mkdir -p $L && cp -a $W/et10src/. $L/ && mkdir -p $L/depends/sources && cp -a $W/endtimes-v5/depends/sources/. $L/depends/sources/
cd $L
step "depends start"
$CAP make -C depends HOST=$H -j2 FALLBACK_DOWNLOAD_PATH=https://dobbscoin.info/depends-sources > $L/../linux-depends.log 2>&1
step "depends done"
./autogen.sh > $L/../linux-autogen.log 2>&1
CONFIG_SITE=$L/depends/$H/share/config.site ./configure --prefix=/ --disable-bench --disable-tests --disable-man > $L/../linux-configure.log 2>&1
grep -E "with (gui|wallet|upnp|natpmp)" $L/../linux-configure.log
$CAP make -j2 > $L/../linux-make.log 2>&1
step "build done"
N=dobbscoin-0.21.2-beta1; rm -rf $L/../$N && mkdir -p $L/../$N/bin
for b in dobbscoind dobbscoin-cli dobbscoin-tx dobbscoin-wallet qt/dobbscoin-qt; do cp src/$b $L/../$N/bin/; done
strip $L/../$N/bin/*; cp COPYING $L/../$N/
cd $L/.. && tar --owner=0 --group=0 --numeric-owner -czf $N-x86_64-linux-gnu.tar.gz $N && sha256sum $N-x86_64-linux-gnu.tar.gz
ls -la $N/bin | cut -c24-; ldd $N/bin/dobbscoin-qt | grep -v -E "linux-vdso|ld-linux|libc.so|libm.so|libpthread|libdl|librt|libgcc_s|libstdc|libX|libxcb|libfontconfig|libfreetype|libz.so|libexpat|libuuid|libbsd|libmd|libbrotli|libpng|libGL" || true
step "packed"

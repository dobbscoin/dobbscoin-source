#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "$repo_root"

./autogen.sh
./configure \
  --with-daemon=yes \
  --with-utils=no \
  --with-libs=no \
  --disable-wallet \
  --with-gui=no \
  --disable-tests \
  --without-miniupnpc
make -j"$(nproc)"

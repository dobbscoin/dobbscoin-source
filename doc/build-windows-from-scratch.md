# Dobbscoin Windows Build From Scratch

Timestamp: `2026-04-01T01:04:22Z`

Build host OS: `Ubuntu 22.04.5 LTS`

Target host: `x86_64-w64-mingw32`

Toolchain: `x86_64-w64-mingw32-g++ (GCC) 12-posix` (Debian 12; Ubuntu 22.04's 10-posix also works)

Dependency versions (`depends/packages/*.mk`, issue #43):

- Qt: `5.15.19` (static, OpenSSL linked)
- Boost: `1.83.0`
- OpenSSL: `3.5.8` (LTS)
- SQLite: `3.38.5` (wallet storage; replaced Berkeley DB 4.8.30)
- miniupnpc: `2.3.3`
- libnatpmp: `20230423`
- Protobuf: `3.21.12` (BIP70 payment requests)
- qrencode: `4.1.1`
- zlib: `1.3.2`

Every tarball is pinned by sha256 and mirrored at
`https://dobbscoin.info/depends-sources/`, which `make -C depends` falls back to
when an upstream URL fails.

## Ubuntu 22.04 / 24.04 workflow

```bash
set -euo pipefail

sudo apt-get update
sudo apt-get install -y \
  autoconf \
  automake \
  bsdmainutils \
  build-essential \
  ccache \
  curl \
  g++-mingw-w64-x86-64-posix \
  git \
  libtool \
  nsis \
  patch \
  pkg-config \
  python3 \
  unzip \
  wget \
  zip

sudo update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
sudo update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix

git clone <YOUR_DOBBSCOIN_REPO_URL> dobbscoin-source
cd dobbscoin-source

make -C depends HOST=x86_64-w64-mingw32 -j"$(nproc)"

./autogen.sh

CONFIG_SITE="$PWD/depends/x86_64-w64-mingw32/share/config.site" \
./configure \
  --host=x86_64-w64-mingw32 \
  --prefix="$PWD/depends/x86_64-w64-mingw32" \
  --with-ssl="$PWD/depends/x86_64-w64-mingw32"

CCACHE_DIR=/tmp/.ccache make -j"$(nproc)"

ls src/qt/dobbscoin-qt.exe src/dobbscoind.exe src/dobbscoin-cli.exe

zip -j dobbscoin-qt-win64.zip \
  src/qt/dobbscoin-qt.exe \
  src/dobbscoind.exe \
  src/dobbscoin-cli.exe
```

## The same with CMake

After `make -C depends`, CMake (3.22 or later) replaces `autogen.sh`, `configure`
and `make`; `nsis` is only needed for the installer:

```bash
cmake -B build-win --toolchain "$PWD/depends/x86_64-w64-mingw32/toolchain.cmake"
cmake --build build-win -j"$(nproc)"
cmake --build build-win --target deploy   # build-win/dobbscoin-<version>-win64-setup.exe

ls build-win/src/qt/dobbscoin-qt.exe build-win/src/dobbscoind.exe build-win/src/dobbscoin-cli.exe
```

## Artifacts

- `src/qt/dobbscoin-qt.exe`
- `src/dobbscoind.exe`
- `src/dobbscoin-cli.exe`
- `dobbscoin-qt-win64.zip`

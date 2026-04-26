# Dobbscoin Windows Build From Scratch

Timestamp: `2026-04-01T01:04:22Z`

Build host OS: `Ubuntu 22.04.5 LTS`

Target host: `x86_64-w64-mingw32`

Toolchain: `x86_64-w64-mingw32-g++ (GCC) 10-win32 20220113`

Dependency versions:

- Qt: `5.2.1`
- Boost: `1.55.0`
- OpenSSL: `1.0.1k`
- Berkeley DB: `4.8.30`
- miniupnpc: `1.9.20140701`
- Protobuf: `2.5.0`
- qrencode: `3.4.3`

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

## Artifacts

- `src/qt/dobbscoin-qt.exe`
- `src/dobbscoind.exe`
- `src/dobbscoin-cli.exe`
- `dobbscoin-qt-win64.zip`

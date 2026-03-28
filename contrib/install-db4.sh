#!/usr/bin/env bash

set -euo pipefail

echo
echo "=========================================="
echo " Dobbscoin Berkeley DB 4.8 Installer"
echo "=========================================="
echo

BDB_VERSION="4.8.30.NC"
BDB_PREFIX="${BDB_PREFIX:-$HOME/db4}"
BDB_TARBALL="db-${BDB_VERSION}.tar.gz"
BDB_SOURCE_DIR="db-${BDB_VERSION}"
BDB_URL="https://download.oracle.com/berkeley-db/${BDB_TARBALL}"

echo "Installing Berkeley DB ${BDB_VERSION}"
echo "Target directory: ${BDB_PREFIX}"
echo

# Skip if already installed
if [ -f "${BDB_PREFIX}/include/db_cxx.h" ] && [ -f "${BDB_PREFIX}/lib/libdb_cxx-4.8.a" ]; then
    echo "Berkeley DB already installed."
    echo
    echo "Headers: ${BDB_PREFIX}/include/db_cxx.h"
    echo "Library: ${BDB_PREFIX}/lib/libdb_cxx-4.8.a"
    exit 0
fi

cd "$HOME"

# Download
if [ ! -f "${BDB_TARBALL}" ]; then
    echo "Downloading Berkeley DB from:"
    echo "${BDB_URL}"
    echo
    wget -O "${BDB_TARBALL}" "${BDB_URL}"
fi

echo
echo "Extracting source..."

rm -rf "${BDB_SOURCE_DIR}"
tar -xzf "${BDB_TARBALL}"

cd "${BDB_SOURCE_DIR}"

echo
echo "Applying modern GCC compatibility patch..."

sed -i 's/__atomic_compare_exchange/__atomic_compare_exchange_db/g' dbinc/atomic.h

mkdir -p build_unix
cd build_unix

echo
echo "Configuring..."

../dist/configure \
    --enable-cxx \
    --disable-shared \
    --with-pic \
    --prefix="${BDB_PREFIX}"

echo
echo "Building..."

make -j1

echo
echo "Installing..."

make install

echo
echo "=========================================="
echo " Berkeley DB 4.8 installed successfully"
echo "=========================================="
echo

echo "Verify installation:"
echo
echo "  ls ${BDB_PREFIX}/include/db_cxx.h"
echo "  ls ${BDB_PREFIX}/lib/libdb_cxx-4.8.a"
echo
echo "Next steps to build Dobbscoin:"
echo
echo "  cd ~/dobbscoin-source"
echo "  ./autogen.sh"
echo "  ./configure \\"
echo "    BDB_LIBS=\"-L${BDB_PREFIX}/lib -ldb_cxx-4.8\" \\"
echo "    BDB_CFLAGS=\"-I${BDB_PREFIX}/include\""
echo "  make -j\$(nproc)"
echo

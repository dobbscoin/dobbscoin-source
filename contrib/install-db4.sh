#!/usr/bin/env bash

set -e

echo
echo "=========================================="
echo " Dobbscoin Berkeley DB 4.8 Installer"
echo "=========================================="
echo

BDB_VERSION="4.8.30.NC"
BDB_PREFIX="$HOME/db4"
BDB_TARBALL="db-$BDB_VERSION.tar.gz"
BDB_SOURCE_DIR="$HOME/db-$BDB_VERSION"

cd "$HOME"

echo "Installing Berkeley DB $BDB_VERSION"
echo "Target directory:"
echo "$BDB_PREFIX"
echo

# Skip if already installed
if [ -f "$BDB_PREFIX/include/db_cxx.h" ]; then
    echo "Berkeley DB already installed at:"
    echo "$BDB_PREFIX"
    exit 0
fi

# Download source
if [ ! -f "$BDB_TARBALL" ]; then
    echo "Downloading Berkeley DB source..."
    wget https://download.oracle.com/berkeley-db/$BDB_TARBALL
fi

echo
echo "Extracting source..."

rm -rf "$BDB_SOURCE_DIR"
tar -xzf "$BDB_TARBALL"

cd "$BDB_SOURCE_DIR/build_unix"

echo
echo "Configuring build..."

# Critical compatibility flags for modern compilers

export CCFLAGS="-std=gnu89"
export CFLAGS="-std=gnu89 -fPIC"
export CXXFLAGS="-std=gnu++98 -fPIC"
export LDFLAGS="-pthread"

../dist/configure \
    --enable-cxx \
    --disable-shared \
    --with-pic \
    --prefix="$BDB_PREFIX"

echo
echo "Building Berkeley DB..."

make clean

make -j$(nproc)

echo
echo "Installing Berkeley DB..."

make install

echo
echo "=========================================="
echo " Berkeley DB 4.8 installed successfully"
echo "=========================================="
echo

echo "Verify installation:"
echo

echo "ls $BDB_PREFIX/include/db_cxx.h"

echo
echo "Then build Dobbscoin with:"
echo

echo "./configure --with-bdb=$BDB_PREFIX"
echo

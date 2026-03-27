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

echo "Installing Berkeley DB $BDB_VERSION"
echo "Target directory:"
echo "$BDB_PREFIX"
echo

cd "$HOME"

# If already installed, exit cleanly
if [ -f "$BDB_PREFIX/include/db_cxx.h" ]; then
    echo "Berkeley DB already installed at:"
    echo "$BDB_PREFIX"
    echo
    echo "Nothing to do."
    exit 0
fi

# Download source if missing
if [ ! -f "$BDB_TARBALL" ]; then
    echo "Downloading Berkeley DB source..."
    wget https://download.oracle.com/berkeley-db/$BDB_TARBALL
else
    echo "Source archive already present."
fi

echo
echo "Extracting source..."

rm -rf "$BDB_SOURCE_DIR"
tar -xzf "$BDB_TARBALL"

cd "$BDB_SOURCE_DIR/build_unix"

echo
echo "Configuring build..."

# Fix for modern GCC atomic conflicts
export CFLAGS="-std=gnu89"
export CXXFLAGS="-std=gnu++98"

../dist/configure \
    --enable-cxx \
    --disable-shared \
    --with-pic \
    --prefix="$BDB_PREFIX"

echo
echo "Building Berkeley DB..."

make -j$(nproc)

echo
echo "Installing Berkeley DB..."

make install

echo
echo "=========================================="
echo " Berkeley DB 4.8 installed successfully"
echo "=========================================="
echo
echo "Location:"
echo "$BDB_PREFIX"
echo
echo "Verify installation with:"
echo
echo "ls $BDB_PREFIX/include/db_cxx.h"
echo
echo "To build Dobbscoin with compatible BDB:"
echo
echo "./configure --with-bdb=$BDB_PREFIX"
echo

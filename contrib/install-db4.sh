#!/usr/bin/env bash

set -e

echo
echo "=========================================="
echo " Dobbscoin Berkeley DB 4.8 Installer"
echo "=========================================="
echo

BDB_VERSION="4.8.30.NC"
BDB_PREFIX="$HOME/db4"

cd "$HOME"

echo "Installing Berkeley DB $BDB_VERSION"
echo "Target directory: $BDB_PREFIX"
echo

# Skip if already installed
if [ -f "$BDB_PREFIX/include/db_cxx.h" ]; then
    echo "Berkeley DB already installed."
    exit 0
fi

# Download
if [ ! -f "db-$BDB_VERSION.tar.gz" ]; then
    echo "Downloading Berkeley DB..."
    wget https://download.oracle.com/berkeley-db/db-$BDB_VERSION.tar.gz
fi

echo
echo "Extracting source..."

rm -rf db-$BDB_VERSION
tar -xzf db-$BDB_VERSION.tar.gz

cd db-$BDB_VERSION

echo
echo "Applying modern GCC compatibility patch..."

sed -i 's/__atomic_compare_exchange/__atomic_compare_exchange_db/g' dbinc/atomic.h

cd build_unix

echo
echo "Configuring..."

../dist/configure \
    --enable-cxx \
    --disable-shared \
    --with-pic \
    --prefix="$BDB_PREFIX"

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
echo "Verify with:"
echo "ls $BDB_PREFIX/include/db_cxx.h"
echo
echo "Then build Dobbscoin:"
echo
echo "./configure --with-bdb=$BDB_PREFIX"

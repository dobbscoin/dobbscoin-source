#!/usr/bin/env bash

set -e

echo
echo "=========================================="
echo " Dobbscoin Berkeley DB 4.8 Installer"
echo "=========================================="
echo

BDB_VERSION="4.8.30.NC"
BDB_PREFIX="$HOME/db4"

echo "Installing Berkeley DB $BDB_VERSION"
echo "Target directory:"
echo "$BDB_PREFIX"
echo

cd "$HOME"

if [ ! -f "db-$BDB_VERSION.tar.gz" ]; then
    echo "Downloading Berkeley DB..."
    wget https://download.oracle.com/berkeley-db/db-$BDB_VERSION.tar.gz
fi

echo
echo "Extracting source..."

tar -xzf db-$BDB_VERSION.tar.gz

cd db-$BDB_VERSION/build_unix

echo
echo "Configuring build..."

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
echo "To build Dobbscoin with compatible BDB:"
echo
echo "./configure --with-bdb=$BDB_PREFIX"
echo

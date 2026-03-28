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

##############################################################################
# Skip if already installed
##############################################################################

if [ -f "${BDB_PREFIX}/include/db_cxx.h" ] && \
   [ -f "${BDB_PREFIX}/lib/libdb_cxx-4.8.a" ]; then

    echo "Berkeley DB already installed."
    echo
    echo "Headers: ${BDB_PREFIX}/include/db_cxx.h"
    echo "Library: ${BDB_PREFIX}/lib/libdb_cxx-4.8.a"

else

    cd "$HOME"

    ##########################################################################
    # Download
    ##########################################################################

    if [ ! -f "${BDB_TARBALL}" ]; then
        echo "Downloading Berkeley DB..."
        echo "${BDB_URL}"
        echo
        wget -O "${BDB_TARBALL}" "${BDB_URL}"
    fi

    ##########################################################################
    # Extract
    ##########################################################################

    echo
    echo "Extracting source..."

    rm -rf "${BDB_SOURCE_DIR}"
    tar -xzf "${BDB_TARBALL}"

    cd "${BDB_SOURCE_DIR}"

    ##########################################################################
    # Modern GCC compatibility patch
    ##########################################################################

    echo
    echo "Applying modern GCC compatibility patch..."

    sed -i 's/__atomic_compare_exchange/__atomic_compare_exchange_db/g' dbinc/atomic.h

    mkdir -p build_unix
    cd build_unix

    ##########################################################################
    # Configure
    ##########################################################################

    echo
    echo "Configuring Berkeley DB..."

    ../dist/configure \
        --enable-cxx \
        --disable-shared \
        --with-pic \
        --prefix="${BDB_PREFIX}"

    ##########################################################################
    # Build
    ##########################################################################

    echo
    echo "Building Berkeley DB..."

    make -j1

    ##########################################################################
    # Install
    ##########################################################################

    echo
    echo "Installing Berkeley DB..."

    make install

    echo
    echo "=========================================="
    echo " Berkeley DB 4.8 installed successfully"
    echo "=========================================="
    echo

fi

##############################################################################
# Export deterministic build environment
##############################################################################

export BDB_PREFIX="${BDB_PREFIX}"
export CPPFLAGS="-I${BDB_PREFIX}/include"
export LDFLAGS="-L${BDB_PREFIX}/lib"

echo
echo "Build environment configured:"
echo
echo "  BDB_PREFIX=${BDB_PREFIX}"
echo "  CPPFLAGS=${CPPFLAGS}"
echo "  LDFLAGS=${LDFLAGS}"
echo

##############################################################################
# Verification
##############################################################################

echo "Verify installation:"
echo
echo "  ls ${BDB_PREFIX}/include/db_cxx.h"
echo "  ls ${BDB_PREFIX}/lib/libdb_cxx-4.8.a"
echo

##############################################################################
# Next steps (deterministic build instructions)
##############################################################################

echo "Next steps to build Dobbscoin:"
echo

echo "cd ~/dobbscoin-source"
echo
echo "./autogen.sh"
echo
echo "CPPFLAGS=\"-I\$HOME/db4/include\" \\"
echo "LDFLAGS=\"-L\$HOME/db4/lib\" \\"
echo "./configure --without-gui"
echo
echo "make -j\$(nproc)"
echo

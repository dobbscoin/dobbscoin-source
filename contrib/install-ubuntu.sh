#!/usr/bin/env bash

set -euo pipefail

##############################################################################
#
# Dobbscoin Deterministic Build Script
#
# This script builds Dobbscoin but NEVER installs or overwrites binaries.
# Operator performs deployment manually.
#
##############################################################################

echo
echo "=============================================="
echo " Dobbscoin Build Script"
echo "=============================================="
echo

cd "$(dirname "$0")/.."

##############################################################################
# Verify Ubuntu
##############################################################################

if ! grep -qi ubuntu /etc/os-release; then
    echo "ERROR: Ubuntu required."
    exit 1
fi

echo "Detected Ubuntu:"
lsb_release -rs
echo

##############################################################################
# Ask whether to build GUI
##############################################################################

echo
echo "----------------------------------------------"
echo " Build Linux GUI wallet?"
echo
echo " 1) Yes — build dobbscoin-qt"
echo " 2) No  — daemon / CLI only"
echo
echo "Defaulting to NO in 15 seconds..."
echo "----------------------------------------------"
echo

read -t 15 -p "Select option (1 or 2): " GUI_CHOICE || true

echo

if [ "${GUI_CHOICE:-2}" = "1" ]; then
    BUILD_GUI="yes"
else
    BUILD_GUI="no"
fi

##############################################################################
# Install dependencies
##############################################################################

echo
echo "Updating packages..."

sudo apt update

echo
echo "Installing build dependencies..."

BASE_PACKAGES=(
    build-essential
    automake
    libtool
    pkg-config
    libssl-dev
    libevent-dev
    bsdmainutils
    curl
    git
    wget
    unzip
    ca-certificates
    libboost-system-dev
    libboost-filesystem-dev
    libboost-chrono-dev
    libboost-program-options-dev
    libboost-test-dev
    libboost-thread-dev
)

QT_PACKAGES=(
    qtbase5-dev
    qttools5-dev
    qttools5-dev-tools
    libqt5gui5
    libqt5core5a
    libqt5dbus5
    libqt5widgets5
    libqrencode-dev
)

sudo apt install -y "${BASE_PACKAGES[@]}"

if [ "$BUILD_GUI" = "yes" ]; then
    sudo apt install -y "${QT_PACKAGES[@]}"
fi

##############################################################################
# Install Berkeley DB
##############################################################################

echo
echo "Installing Berkeley DB 4.8..."

bash contrib/install-db4.sh

##############################################################################
# Build
##############################################################################

echo
echo "Preparing build..."

./autogen.sh

echo
echo "Configuring..."

if [ "$BUILD_GUI" = "yes" ]; then

    CPPFLAGS="-I$HOME/db4/include" \
    LDFLAGS="-L$HOME/db4/lib" \
    ./configure

else

    CPPFLAGS="-I$HOME/db4/include" \
    LDFLAGS="-L$HOME/db4/lib" \
    ./configure --without-gui

fi

echo
echo "Building..."

make -j$(nproc)

##############################################################################
# Verify build
##############################################################################

echo
echo "Verifying build..."

REQUIRED_BINS=(
    src/dobbscoind
    src/dobbscoin-cli
    src/dobbscoin-tx
)

for BIN in "${REQUIRED_BINS[@]}"; do
    if [ ! -f "$BIN" ]; then
        echo
        echo "ERROR: Missing binary:"
        echo "$BIN"
        exit 1
    fi
done

if [ "$BUILD_GUI" = "yes" ]; then
    if [ ! -f src/qt/dobbscoin-qt ]; then
        echo
        echo "ERROR: Missing GUI binary"
        exit 1
    fi
fi

##############################################################################
# Success message
##############################################################################

echo
echo "=============================================="
echo " Build complete"
echo "=============================================="
echo

echo "Binaries are located at:"
echo

echo "  src/dobbscoind"
echo "  src/dobbscoin-cli"
echo "  src/dobbscoin-tx"

if [ "$BUILD_GUI" = "yes" ]; then
    echo "  src/qt/dobbscoin-qt"
fi

echo
echo "No files were installed or overwritten."
echo

echo "Deploy manually when ready:"
echo

echo "  cp src/dobbscoind ~/dobbscoin/"
echo "  cp src/dobbscoin-cli ~/dobbscoin/"
echo "  cp src/dobbscoin-tx ~/dobbscoin/"
echo

echo "=============================================="
echo

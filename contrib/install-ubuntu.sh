#!/usr/bin/env bash

set -euo pipefail

##############################################################################
#
# Dobbscoin Deterministic Ubuntu Installer
#
# This script installs dependencies, installs Berkeley DB 4.8 locally,
# builds Dobbscoin, and installs binaries system-wide.
#
# Supported:
#   Ubuntu 22.04
#   Ubuntu 24.04
#
# Usage:
#
#   git clone https://github.com/dobbscoin/dobbscoin-source.git
#   cd dobbscoin-source
#   bash contrib/install-ubuntu.sh
#
##############################################################################

echo
echo "=============================================="
echo " Dobbscoin Deterministic Ubuntu Installer"
echo "=============================================="
echo

##############################################################################
# Move to repo root
##############################################################################

cd "$(dirname "$0")/.."

##############################################################################
# Verify Ubuntu
##############################################################################

if ! grep -qi ubuntu /etc/os-release; then
    echo "ERROR: Ubuntu required."
    exit 1
fi

echo "Detected Ubuntu version:"
lsb_release -rs
echo

##############################################################################
# Install system dependencies
##############################################################################

echo "Updating package lists..."

sudo apt update

echo
echo "Installing build dependencies..."

sudo apt install -y \
    build-essential \
    automake \
    libtool \
    pkg-config \
    libssl-dev \
    libevent-dev \
    bsdmainutils \
    software-properties-common \
    curl \
    git \
    wget \
    unzip \
    ca-certificates \
    libboost-system-dev \
    libboost-filesystem-dev \
    libboost-chrono-dev \
    libboost-program-options-dev \
    libboost-test-dev \
    libboost-thread-dev \
    qtbase5-dev \
    qttools5-dev \
    qttools5-dev-tools \
    libqt5gui5 \
    libqt5core5a \
    libqt5dbus5 \
    libqt5widgets5 \
    libqrencode-dev

##############################################################################
# Install Berkeley DB 4.8
##############################################################################

echo
echo "Installing Berkeley DB 4.8..."

chmod +x contrib/install-db4.sh
bash contrib/install-db4.sh

##############################################################################
# Build Dobbscoin
##############################################################################

echo
echo "Preparing build..."

if [ ! -f configure ]; then
    ./autogen.sh
fi

echo
echo "Configuring build environment..."

CPPFLAGS="-I$HOME/db4/include" \
LDFLAGS="-L$HOME/db4/lib" \
./configure

##############################################################################
# Compile
##############################################################################

echo
echo "Building Dobbscoin..."

make -j$(nproc)

##############################################################################
# Install
##############################################################################

echo
echo "Installing binaries..."

sudo make install

##############################################################################
# Verify installation
##############################################################################

echo
echo "Verifying installation..."

echo
echo "dobbscoind version:"
dobbscoind --version || true

echo
echo "dobbscoin-qt version:"
dobbscoin-qt --version || true

echo
echo "=============================================="
echo " Dobbscoin installation complete"
echo "=============================================="
echo

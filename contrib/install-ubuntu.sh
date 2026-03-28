#!/usr/bin/env bash

set -euo pipefail

##############################################################################
#
# Dobbscoin Deterministic Installer
#
# Local install:
#   ~/dobbscoin
#
# System install:
#   /usr/local/bin
#
# Prompts:
#   Build GUI? (default NO after 15 seconds)
#   Install location? (default LOCAL after 15 seconds)
#
##############################################################################

echo
echo "=============================================="
echo " Dobbscoin Deterministic Installer"
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
    echo "GUI build ENABLED"
else
    BUILD_GUI="no"
    echo "GUI build DISABLED"
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
# Install Berkeley DB 4.8
##############################################################################

echo
echo "Installing Berkeley DB 4.8..."

bash contrib/install-db4.sh

##############################################################################
# Build Dobbscoin
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
# Ask install location
##############################################################################

echo
echo "----------------------------------------------"
echo " Install location:"
echo
echo " 1) Local user install (~/dobbscoin)"
echo " 2) System-wide install (/usr/local/bin)"
echo
echo "Defaulting to LOCAL install in 15 seconds..."
echo "----------------------------------------------"
echo

read -t 15 -p "Select option (1 or 2): " INSTALL_CHOICE || true

echo

if [ "${INSTALL_CHOICE:-1}" = "2" ]; then

    echo "Installing system-wide..."

    sudo make install

    INSTALL_PATH="/usr/local/bin"

else

    echo "Installing locally..."

    INSTALL_PATH="$HOME/dobbscoin"

    mkdir -p "$INSTALL_PATH"

    echo
    echo "Copying binaries..."

    cp src/dobbscoind "$INSTALL_PATH/"
    cp src/dobbscoin-cli "$INSTALL_PATH/"
    cp src/dobbscoin-tx "$INSTALL_PATH/"

    if [ "$BUILD_GUI" = "yes" ]; then
        cp src/qt/dobbscoin-qt "$INSTALL_PATH/"
    fi

    chmod +x "$INSTALL_PATH"/*

    echo
    echo "Stripping binaries..."

    strip "$INSTALL_PATH"/dobbscoind || true
    strip "$INSTALL_PATH"/dobbscoin-cli || true
    strip "$INSTALL_PATH"/dobbscoin-tx || true

    if [ "$BUILD_GUI" = "yes" ]; then
        strip "$INSTALL_PATH"/dobbscoin-qt || true
    fi

fi

##############################################################################
# Ensure data directory exists
##############################################################################

mkdir -p "$HOME/.dobbscoin"

##############################################################################
# Verify required binaries
##############################################################################

echo
echo "Verifying installation..."

for BIN in dobbscoind dobbscoin-cli dobbscoin-tx; do
    if [ ! -f "$INSTALL_PATH/$BIN" ]; then
        echo "ERROR: $BIN not installed"
        exit 1
    fi
done

##############################################################################
# PATH guidance for local installs
##############################################################################

if [ "$INSTALL_PATH" = "$HOME/dobbscoin" ]; then

    echo
    echo "Optional: add Dobbscoin to your PATH"
    echo
    echo "Add this line to ~/.bashrc or ~/.profile:"
    echo
    echo 'export PATH="$HOME/dobbscoin:$PATH"'
    echo

fi

##############################################################################
# Final output
##############################################################################

echo
echo "=============================================="
echo " Installation complete"
echo "=============================================="
echo

echo "Installed to:"
echo
echo "  $INSTALL_PATH"
echo

echo "Available binaries:"
echo

ls -1 "$INSTALL_PATH"

echo
echo "Test command:"
echo

echo "  $INSTALL_PATH/dobbscoind --version"
echo

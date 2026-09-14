#!/usr/bin/env bash
#
# Self-contained runner for parent-pow-tests.py.
#
# Starts a throwaway regtest dobbscoind, generates enough blocks to cross
# HARDFORK_AUXPOW_TESTNET (10), runs the AuxPoW parent-PoW suite against it,
# and tears the whole thing down again.
#
# Usage: ./run-parent-pow-tests.sh [path/to/dobbscoind] [path/to/dobbscoin-cli]
# Defaults assume cwd = qa/auxpow-smoke/.
set -euo pipefail

DOBBSCOIND="${1:-../../src/dobbscoind}"
CLI="${2:-../../src/dobbscoin-cli}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATADIR="$(mktemp -d)"
RPCPORT=$(( 30000 + RANDOM % 20000 ))
CLI_ARGS=(-regtest -datadir="$DATADIR" -rpcport="$RPCPORT" -rpcwait -rpcclienttimeout=300)

cleanup() {
    if [[ -f "$DATADIR/regtest/dobbscoind.pid" ]]; then
        "$CLI" "${CLI_ARGS[@]}" stop >/dev/null 2>&1 || true
        sleep 1
    fi
    rm -rf "$DATADIR"
}
trap cleanup EXIT

# parent-pow-tests.py reads rpcport/rpcuser/rpcpassword out of this file.
cat > "$DATADIR/dobbscoin.conf" <<EOF
regtest=1
rpcuser=auxpow
rpcpassword=test
rpcport=$RPCPORT
EOF

echo "==> Starting regtest dobbscoind in $DATADIR"
"$DOBBSCOIND" -regtest -datadir="$DATADIR" -daemon \
              -rpcuser=auxpow -rpcpassword=test \
              -rpcport="$RPCPORT" \
              -listen=0 -discover=0 \
              >/dev/null 2>&1

"$CLI" "${CLI_ARGS[@]}" getblockcount >/dev/null

# Cross the activation height. HARDFORK_AUXPOW_TESTNET is 10; 15 leaves room
# for the suite to append blocks of its own.
echo "==> Generating 15 blocks (AuxPoW activates at 10)"
"$CLI" "${CLI_ARGS[@]}" setgenerate true 15 >/dev/null

python3 "$HERE/parent-pow-tests.py" \
        "$DATADIR/dobbscoin.conf" \
        "$DATADIR/regtest/debug.log"

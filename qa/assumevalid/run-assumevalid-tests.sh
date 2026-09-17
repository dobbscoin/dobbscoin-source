#!/usr/bin/env bash
#
# Self-contained runner for the -assumevalid on-chain tests.
#
# Starts a throwaway regtest dobbscoind (node A) that mines the chain by hand,
# then the suite starts and restarts a second node (B) against it with
# -assumevalid set different ways. B is what is under test: -assumevalid can
# only fire during headers-first sync, which a single node cannot produce.
#
# Usage: ./run-assumevalid-tests.sh [path/to/dobbscoind] [path/to/dobbscoin-cli]
# Defaults assume cwd = qa/assumevalid/.
set -euo pipefail

DOBBSCOIND="${1:-../../src/dobbscoind}"
CLI="${2:-../../src/dobbscoin-cli}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOBBSCOIND="$(cd "$(dirname "$DOBBSCOIND")" && pwd)/$(basename "$DOBBSCOIND")"
DATADIR="$(mktemp -d)"
RPCPORT_A=$(( 30000 + RANDOM % 10000 ))
RPCPORT_B=$(( RPCPORT_A + 1 ))
PORT_A=$(( RPCPORT_A + 2 ))
CLI_ARGS=(-regtest -datadir="$DATADIR/a" -rpcport="$RPCPORT_A" -rpcwait -rpcclienttimeout=300
          -rpcuser=av -rpcpassword=test)

cleanup() {
    for d in a b; do
        if [[ -f "$DATADIR/$d/regtest/dobbscoind.pid" ]]; then
            kill "$(cat "$DATADIR/$d/regtest/dobbscoind.pid")" 2>/dev/null || true
        fi
    done
    sleep 2
    for d in a b; do
        if [[ -f "$DATADIR/$d/regtest/dobbscoind.pid" ]]; then
            kill -9 "$(cat "$DATADIR/$d/regtest/dobbscoind.pid")" 2>/dev/null || true
        fi
    done
    rm -rf "$DATADIR"
}
trap cleanup EXIT

mkdir -p "$DATADIR/a"
cat > "$DATADIR/a/dobbscoin.conf" <<CONF
regtest=1
rpcuser=av
rpcpassword=test
rpcport=$RPCPORT_A
CONF

echo "==> Starting regtest node A in $DATADIR/a (p2p $PORT_A)"
"$DOBBSCOIND" -regtest -datadir="$DATADIR/a" -daemon \
              -rpcuser=av -rpcpassword=test -rpcport="$RPCPORT_A" \
              -listen=1 -port="$PORT_A" -discover=0 \
              >/dev/null 2>&1

"$CLI" "${CLI_ARGS[@]}" getblockcount >/dev/null

python3 -u "$HERE/assumevalid-chain-tests.py" \
        "$DATADIR/a/dobbscoin.conf" "$DATADIR/b" "$DOBBSCOIND" "$PORT_A" "$RPCPORT_B"

#!/usr/bin/env bash
#
# Self-contained runner for the signature-normalisation test.
#
# Starts a throwaway regtest dobbscoind, runs the suite against it, tears it
# down. The suite mines its own blocks by hand -- the standard build has the
# wallet compiled out, so there is no setgenerate and no funding source.
#
# Usage: ./run-cltv-chain-tests.sh [path/to/dobbscoind] [path/to/dobbscoin-cli]
# Defaults assume cwd = qa/cltv/.
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

cat > "$DATADIR/dobbscoin.conf" <<EOF
regtest=1
rpcuser=cltv
rpcpassword=test
rpcport=$RPCPORT
EOF

echo "==> Starting regtest dobbscoind in $DATADIR"
"$DOBBSCOIND" -regtest -datadir="$DATADIR" -daemon \
              -rpcuser=cltv -rpcpassword=test \
              -rpcport="$RPCPORT" \
              -listen=0 -discover=0 \
              >/dev/null 2>&1

"$CLI" "${CLI_ARGS[@]}" getblockcount >/dev/null

python3 "$HERE/sig-normalize-chain-test.py" "$DATADIR/dobbscoin.conf"

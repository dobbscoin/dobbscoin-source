#!/usr/bin/env bash
# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Pool-mode test of the wallet's stratum client, with no real pool and no
# network: an isolated two-node TESTNET on 127.0.0.1 plus qa/stratum/stratum-server.py.
#
#   POOL node   (POOLD)   serves getblocktemplate/submitblock to the stratum
#                         server. Run it on a build you trust (e.g. unmodified
#                         main): every block found through stratum must pass
#                         its submitblock.
#   MINER node  (MINERD)  the build under test; setstratum points its stratum
#                         client at the server, once per -minerscrypt value.
#
#   POOLD=... POOLCLI=... MINERD=... MINERCLI=... qa/stratum/run-stratum-test.sh
#
# Optional: IMPLS="generic avx2" (default: generic sse2 avx avx2; "-" = do not
# pass -minerscrypt, for builds that predate it), SECS=60 per implementation,
# THREADS=8, DIFF=1 (share difficulty), WORKDIR (default: a fresh mktemp dir).
# MINERD may be a wrapper script (e.g. one running dobbscoind.exe under wine).
#
# Exits non-zero if any implementation got no accepted share, any share was
# rejected, a found block was rejected, or the two nodes end on different tips.
set -u
POOLD=${POOLD:?}; POOLCLI=${POOLCLI:?}; MINERD=${MINERD:?}; MINERCLI=${MINERCLI:?}
IMPLS=${IMPLS:-"generic sse2 avx avx2"}; SECS=${SECS:-60}; THREADS=${THREADS:-8}; DIFF=${DIFF:-1}
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
W=${WORKDIR:-$(mktemp -d /tmp/stratum-test.XXXXXX)}; P=$W/pool; M=$W/miner; mkdir -p $P $M
PORT_BASE=${PORT_BASE:-29460}
PRPC=$((PORT_BASE)); PP2P=$((PORT_BASE+1)); MRPC=$((PORT_BASE+2)); MP2P=$((PORT_BASE+3)); SPORT=$((PORT_BASE+4))
AUTH="-rpcuser=st -rpcpassword=stpass"
NET="-testnet -bind=127.0.0.1 -listen=1 -dnsseed=0 -discover=0 -printtoconsole=0"
cliP() { $POOLCLI -testnet -datadir=$P $AUTH -rpcport=$PRPC "$@"; }
cliM() { $MINERCLI -testnet -datadir=$M $AUTH -rpcport=$MRPC "$@"; }
waitrpc() { for _ in $(seq 1 240); do "$@" getblockcount >/dev/null 2>&1 && return 0; sleep 0.5; done; echo "rpc never came up"; exit 1; }
json() { python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print(d$2)" "$1"; }
fail=0

$POOLD -datadir=$P $AUTH -rpcport=$PRPC -port=$PP2P $NET -connect=127.0.0.1:$MP2P & PIDP=$!
waitrpc cliP
startM() { $MINERD -datadir=$M $AUTH -rpcport=$MRPC -port=$MP2P $NET -connect=127.0.0.1:$PP2P "$@" & PIDM=$!; waitrpc cliM; }
stopM() { cliM stop >/dev/null 2>&1; wait $PIDM; }

startM
ADDR=$(cliM getnewaddress)
stopM
echo "workdir $W; payout address $ADDR; share difficulty $DIFF; $THREADS threads; ${SECS}s per implementation"

for impl in $IMPLS; do
    STATS=$W/stats-$impl.json
    python3 "$HERE/stratum-server.py" --rpcport $PRPC --rpcuser st --rpcpassword stpass --port $SPORT \
        --difficulty $DIFF --stats-file $STATS > $W/server-$impl.log 2>&1 & PIDS=$!
    sleep 1
    if [ "$impl" = "-" ]; then startM; else startM -minerscrypt=$impl; fi
    for _ in $(seq 1 60); do cliP getconnectioncount | grep -qv '^0$' && break; sleep 0.5; done
    h0=$(cliP getblockcount)
    cliM setstratum true "127.0.0.1:$SPORT" "$ADDR" $THREADS >/dev/null
    samples=""
    for _ in $(seq 1 $((SECS / 5))); do
        sleep 5
        samples="$samples $(cliM getstratuminfo | python3 -c 'import json,sys; print(int(json.load(sys.stdin)["hashespersec"]))')"
    done
    info=$(cliM getstratuminfo | python3 -c 'import json,sys; d=json.load(sys.stdin); print("client: submitted %d accepted %d rejected %d" % (d["sharessubmitted"], d["sharesaccepted"], d["sharesrejected"]))' 2>/dev/null)
    cliM setstratum false >/dev/null
    used=$(grep -a "Scrypt miner: using" $M/testnet3/debug.log | tail -1 | sed 's/.*Scrypt miner: using //')
    stopM
    kill $PIDS; wait $PIDS 2>/dev/null
    h1=$(cliP getblockcount)
    acc=$(json $STATS '["shares_accepted"]'); rej=$(json $STATS '["shares_rejected"]')
    bsub=$(json $STATS '["blocks_submitted"]'); bacc=$(json $STATS '["blocks_accepted"]')
    span=$(json $STATS '["last_share_time"]-d["first_share_time"]' 2>/dev/null || echo 0)
    echo "impl=$impl [${used:-no -minerscrypt}] client hashespersec:$samples"
    echo "    server: shares accepted $acc rejected $rej ($(json $STATS '["reject_reasons"]')); $info"
    echo "    server estimate from shares: $(python3 -c "print(int($acc*65536*$DIFF/max($span,1)))" 2>/dev/null) H/s over ${span%.*}s; blocks via stratum: submitted $bsub accepted $bacc; pool node height $h0 -> $h1"
    [ "$acc" -gt 0 ] && [ "$rej" -eq 0 ] && [ "$bsub" -eq "$bacc" ] || fail=1
    [ "$(grep -c DISAGREES $M/testnet3/debug.log)" -eq 0 ] || { echo "    fast/generic DISAGREEMENT logged"; fail=1; }
done

startM
TIP=$(cliP getbestblockhash)
for _ in $(seq 1 120); do [ "$(cliM getbestblockhash)" = "$TIP" ] && break; sleep 0.5; done
echo "--- pool node:  height $(cliP getblockcount) tip $TIP verifychain $(cliP verifychain 4 0)"
echo "--- miner node: height $(cliM getblockcount) tip $(cliM getbestblockhash)"
[ "$(cliM getbestblockhash)" = "$TIP" ] || fail=1
stopM; cliP stop >/dev/null; wait $PIDP
[ $fail -eq 0 ] && echo "RESULT: PASS" || echo "RESULT: FAIL"
exit $fail
